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

import os
import sys
import urllib
import urlparse

from twisted.internet import reactor
from twisted.internet import protocol
#from twisted.web import client
from keepalive import Proxy
from BTL.bencode import bdecode

from BTL.greenlet_yielddefer import like_yield, coroutine

class BTFake(protocol.Protocol):
    def connectionMade(self):
        self.buffer = ''
    def dataReceived(self, data):
        self.buffer += data
        # ident + reserved + info + peer_id
        if len(self.buffer) >= 68:
            self.transport.write(self.buffer[:48] + "MAGICMAGICMAGICMAGIC")
            self.transport.loseConnection()

f = protocol.Factory()
f.protocol = BTFake
reactor.listenTCP(6881, f)

f = open(sys.argv[1], "r")
if len(sys.argv) > 2:
    baseurl = sys.argv[2]
else:
    baseurl = "http://localhost:8000"

all_lines = []

for line in f.readlines():
    if line.startswith("REQUEST: "):
        url = baseurl + line[9:].strip()
        all_lines.append(url)

to_remove = set()
logiter = iter(all_lines)

id_map = {}

def make_id():
    return 'MAGICMAG' + os.urandom(6).encode('hex')

def get_param(url, param_name):
    if param_name not in url:
        return None
    b, e = url.split(param_name, 1)
    val, e = e.split('&', 1)
    val = val[1:] # '='
    return val

def replace_param(url, param_name, newval):
    i = url.find(param_name)
    if i == -1:
        return url
    e = url[i+len(param_name):]
    url = url[:i] + param_name + '=' + urllib.quote(newval)
    i = e.find('&')
    if i != -1:
        url += '&' + e[i+1:]
    return url

@coroutine
def main(proxy):
    global logiter
    global all_lines
    global to_remove
    for url in logiter:
        try:
            peer_id = get_param(url, "peer_id")
            if peer_id:
                peer_id = id_map.setdefault(peer_id, make_id())
                url = replace_param(url, "peer_id", peer_id)
            url = replace_param(url, "ip", "127.0.0.1")
            url = replace_param(url, "port", "6881")

            path = '/' + url.split('/', 3)[3]
            sys.stdout.write('.')
            df = proxy.callRemote(path)
            r = like_yield(df)
            r = bdecode(r)
            er = r.get('failure reason')
            if er:
                raise Exception(er)
        except Exception, e:
            print "Error", e.__class__, e, url
            to_remove.add(line)
            pass
##            try:
##                reactor.stop()
##            except:
##                pass
    # start over
    print "restarting!", len(to_remove)
    id_map.clear()
    #all_lines = [ x for x in all_lines if x not in to_remove ]
    to_remove = set()
    logiter = iter(all_lines)
    reactor.callLater(0, main, proxy)

for x in xrange(100):
    reactor.callLater(0, main, Proxy(baseurl))

reactor.run()

