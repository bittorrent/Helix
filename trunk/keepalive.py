#The MIT License
#
#Copyright (c) 2009 BitTorrent Inc.
#
#Permission is hereby granted, free of charge, to any person obtaining a copy
#of this software and associated documentation files (the "Software"), to deal
#in the Software without restriction, including without limitation the rights
#to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#copies of the Software, and to permit persons to whom the Software is
#furnished to do so, subject to the following conditions:
#
#The above copyright notice and this permission notice shall be included in
#all copies or substantial portions of the Software.
#
#THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#THE SOFTWARE.

import urlparse
from cStringIO import StringIO
from gzip import GzipFile

from twisted.web import resource, server
from twisted.internet import protocol, defer, reactor
from twisted.python import log, reflect, failure
from twisted.web import http

from twisted.internet import protocol
#from BTL.decorate import decorate_func
def decorate_func(new, old):
    def runner(*a, **kw):
        new(*a, **kw)
        return old(*a, **kw)
    return runner

## someday twisted might do this for me
class SmartReconnectingClientFactory(protocol.ReconnectingClientFactory):

    # SUPER AGRO SETTINGS
    maxDelay = 800
    initialDelay = 1.0
    factor = 1
    jitter = 0

    def buildProtocol(self, addr):
        prot = protocol.ReconnectingClientFactory.buildProtocol(self, addr)

        # decorate the protocol with a delay reset
        prot.connectionMade = decorate_func(self.resetDelay,
                                            prot.connectionMade)
        
        return prot    

pipeline_debug = False

class Query(object):

    def __init__(self, path, host, user=None, password=None):
        self.path = path
        self.host = host
        self.user = user
        self.password = password
        #self.payload = payload
        self.deferred = defer.Deferred()
        self.decode = False

class QueryProtocol(http.HTTPClient):

    # All current queries are pipelined over the connection at
    # once. When the connection is made, or as queries are made
    # while a connection exists, queries are all sent to the
    # server.  Pipelining limits can be controlled by the caller.
    # When a query completes (see parseResponse), if there are no
    # more queries then an idle timeout gets sets.
    # The QueryFactory reopens the connection if another query occurs.
    #
    # twisted_ebrpc does currently provide a mechanism for 
    # per-query timeouts.   This could be added with another
    # timeout_call mechanism that calls loseConnection and pops the
    # current query with an errback.
    timeout = 300   # idle timeout.

    def log(self, msg, *a):
        print "%s: %s: %r" % (self.peer, msg, a)

    def connectionMade(self):
        http.HTTPClient.connectionMade(self)
        self.current_queries = []
        self.timeout_call = None
        if pipeline_debug:
            p = self.transport.getPeer()
            p = "%s:%d" % (p.host, p.port)
            self.peer = (hex(id(self)), p)
        self.factory.connectionMade(self)

    def _cancelTimeout(self):
        if self.timeout_call and self.timeout_call.active():
            self.timeout_call.cancel()
        self.timeout_call = None

    def connectionLost(self, reason):
        http.HTTPClient.connectionLost(self, reason)
        if pipeline_debug: self.log('connectionLost', reason.getErrorMessage())
        self._cancelTimeout()
        if self.current_queries:
            # queries failed, put them back
            if pipeline_debug: self.log('putting back', [q.path for q in self.current_queries])
            self.factory.prependQueries(self.current_queries)
        self.factory.connectionLost(self)

    def sendCommand(self, command, path):
        self.transport.write('%s %s HTTP/1.1\r\n' % (command, path))

    def setLineMode(self, rest):
        # twisted is stupid.
        self.firstLine = 1
        return http.HTTPClient.setLineMode(self, rest)
    
    def sendQuery(self):
        self._cancelTimeout()

        query = self.factory.popQuery()
        if pipeline_debug: self.log('sending', query.path)
        self.current_queries.append(query)
        self.sendCommand('GET', query.path)
        self.sendHeader('User-Agent', 'Twisted KA Client 1.0')
        self.sendHeader('Host', query.host)
        self.sendHeader('Accept-encoding', 'gzip')
        self.sendHeader('Connection', 'Keep-Alive')
        self.sendHeader('Content-type', 'application/octet-stream')
        #if query.user:
        #    auth = '%s:%s' % (query.user, query.password)
        #    auth = auth.encode('base64').strip()
        #    self.sendHeader('Authorization', 'Basic %s' % (auth,))
        self.endHeaders()
        #self.transport.write(query.payload)

    def parseResponse(self, contents):
        query = self.current_queries.pop(0)
        if pipeline_debug: self.log('responded', query.path)

        if not self.current_queries:
            assert not self.factory.anyQueries()
            assert not self.timeout_call
            self.timeout_call = reactor.callLater(self.timeout,
                                                  self.transport.loseConnection)

        query.deferred.callback(contents)
        del query.deferred

    def badStatus(self, status, message):
        query = self.current_queries.pop(0)
        if pipeline_debug: self.log('failed', status, message, query.path)
        try:
            raise ValueError(status, message)
        except:
            query.deferred.errback(failure.Failure())
        del query.deferred
        self.transport.loseConnection()

    def handleStatus(self, version, status, message):
        if status != '200':
            self.badStatus(status, message)

    def handleHeader(self, key, val):
        if not self.current_queries[0].decode:
            if key.lower() == 'content-encoding' and val.lower() == 'gzip':
                self.current_queries[0].decode = True

    def handleResponse(self, contents):
        if self.current_queries[0].decode:
            s = StringIO()
            s.write(contents)
            s.seek(-1)
            g = GzipFile(fileobj=s, mode='rb')
            contents = g.read()
            g.close()
        self.parseResponse(contents)


class QueryFactory(object):

    def __init__(self):
        self.queries = []
        self.instance = None

    def connectionMade(self, instance):
        assert not self.instance
        self.instance = instance
        if pipeline_debug: print 'connection made %s' % str(instance.peer)
        while self.anyQueries():
            self.instance.sendQuery()

    def connectionLost(self, instance):
        assert self.instance == instance, "%s and %s" % (self.instance, instance)
        if pipeline_debug: print 'connection lost %s' % str(instance.peer)
        self.instance = None

    def prependQueries(self, queries):
        self.queries = queries + self.queries

    def popQuery(self):
        return self.queries.pop(0)

    def anyQueries(self):
        return bool(self.queries)

    def addQuery(self, query):
        self.queries.append(query)
        if pipeline_debug: print 'addQuery: %s %s' % (self.instance, self.queries)
        if self.instance:
            self.instance.sendQuery()

    def disconnect(self):
        if not self.instance:
            return
        if not hasattr(self.instance, 'transport'):
            return
        self.instance.transport.loseConnection()        
        

class PersistantSingletonFactory(QueryFactory, SmartReconnectingClientFactory):

    def clientConnectionFailed(self, connector, reason):
        if pipeline_debug: print 'clientConnectionFailed %s' % str(connector)
        return SmartReconnectingClientFactory.clientConnectionFailed(self, connector, reason)

    def clientConnectionLost(self, connector, unused_reason):
        if pipeline_debug: print 'clientConnectionLost %s' % str(connector)
        if not self.anyQueries():
            self.started = False
            self.continueTrying = False
        return SmartReconnectingClientFactory.clientConnectionLost(self, connector, unused_reason)
    

class SingletonFactory(QueryFactory, protocol.ClientFactory):

    def clientConnectionFailed(self, connector, reason):
        if pipeline_debug: print 'clientConnectionFailed %s' % str(connector)
        queries = list(self.queries)
        del self.queries[:]
        for query in queries:
            query.deferred.errback(reason)
        self.started = False


class Proxy:

    def __init__(self, url, user=None, password=None, retry_forever = True):
        """
        @type url: C{str}
        @param url: The URL to which to post method calls.  Calls will be made
        over SSL if the scheme is HTTPS.  If netloc contains username or
        password information, these will be used to authenticate, as long as
        the C{user} and C{password} arguments are not specified.

        @type user: C{str} or None
        @param user: The username with which to authenticate with the server
        when making calls.  If specified, overrides any username information
        embedded in C{url}.  If not specified, a value may be taken from C{url}
        if present.

        @type password: C{str} or None
        @param password: The password with which to authenticate with the
        server when making calls.  If specified, overrides any password
        information embedded in C{url}.  If not specified, a value may be taken
        from C{url} if present.
        """
        scheme, netloc, path, params, query, fragment = urlparse.urlparse(url)
        netlocParts = netloc.split('@')
        if len(netlocParts) == 2:
            userpass = netlocParts.pop(0).split(':')
            self.user = userpass.pop(0)
            try:
                self.password = userpass.pop(0)
            except:
                self.password = None
        else:
            self.user = self.password = None
        hostport = netlocParts[0].split(':')
        self.host = hostport.pop(0)
        try:
            self.port = int(hostport.pop(0))
        except:
            self.port = None
        self.secure = (scheme == 'https')
        if user is not None:
            self.user = user
        if password is not None:
            self.password = password

        if not retry_forever:
            _Factory = SingletonFactory
        else:
            _Factory = PersistantSingletonFactory
        self.factory = _Factory()
        self.factory.started = False
        self.factory.protocol = QueryProtocol

    def callRemote(self, path):
        if pipeline_debug: print 'callRemote to %s : %s' % (self.host, path)
        query = Query(path, self.host, self.user, self.password)
        self.factory.addQuery(query)

        if pipeline_debug: print 'factory started: %s' % self.factory.started
        if not self.factory.started:
            self.factory.started = True
            def connect(host):
                if self.secure:
                    if pipeline_debug: print 'connecting to %s' % str((host, self.port or 443))
                    from twisted.internet import ssl
                    reactor.connectSSL(host, self.port or 443,
                                       self.factory, ssl.ClientContextFactory(),
                                       timeout=60)
                else:
                    if pipeline_debug: print 'connecting to %s' % str((host, self.port or 80))
                    reactor.connectTCP(host, self.port or 80, self.factory,
                                       timeout=60)
            df = reactor.resolve(self.host)
            df.addCallback(connect)
            df.addErrback(query.deferred.errback)
        return query.deferred


class AsyncServerProxy(object):

    def __init__(self, base_url, username=None, password=None, debug=False, 
                 retry_forever = True):
        self.base_url = base_url
        self.username = username
        self.password = password
        self.proxy = Proxy(self.base_url, self.username, self.password, retry_forever)
        self.debug = debug

    def __getattr__(self, attr):
        return self._make_call(attr)

    def _make_call(self, methodname):
        return lambda *a, **kw : self._method(methodname, *a, **kw)

    def _method(self, methodname, *a, **kw):
        # in case they have changed
        self.proxy.user = self.username
        self.proxy.password = self.password
        if self.debug:
            print ('callRemote:', self.__class__.__name__,
                   self.base_url, methodname, a, kw)
        df = self.proxy.callRemote(methodname, *a, **kw)
        return df
