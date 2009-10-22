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

from urllib import urlopen
from BTL import bencode
from BTL.hash import sha
import sys
url = sys.argv[1]

info_hash = '0' * 20
tid = 'a' * 20

interval_sum = 0
min_interval = 100000
max_interval = 0
num_announces = 0

use_auth = True

sekret = 'sekret'

def scrape(url):
	separator = '?'
	if '?' in url: separator = '&'
	req = '%s%cinfo_hash=%s' % (url.replace('announce', 'scrape'), separator, info_hash)
#	print req
	r = bencode.bdecode(urlopen(req).read())
	return r['files'][info_hash]
	
def announce(url, id, left = 10, event = ''):
	global interval_sum
	global min_interval
	global max_interval
	global num_announces

	auth = ''
	if use_auth:
		auth = 'auth=%s&' % sha(info_hash + tid + sekret).hexdigest()

	if event != '': event = '&event=%s' % event
	separator = '?'
	if '?' in url: separator = '&'
	req = '%s%c%sinfo_hash=%s&tid=%s&peer_id=DNA%0.4d%s&left=%d&port=%d%s' % (url, separator, auth, info_hash, tid, id, '0' * 13, left, id, event)
#	print req
	r = bencode.bdecode(urlopen(req).read())
	if not 'peers' in r:
		return []
	peers = r['peers']
	peers6 = ''
	try:
		peers6 = r['peers6']
	except: pass
	interval = r['interval']
	interval_sum += interval
	if interval < min_interval: min_interval = interval
	if interval > max_interval: max_interval = interval
	num_announces += 1
	ret = []
	while len(peers) >= 6:
		ret.append(peers[5:6])
		peers = peers[6:]
	while len(peers6) >= 18:
		ret.append(peers6[17:18])
		peers6 = peers6[18:]
	ret = sorted(ret)
#	print ret
	return ret

errors = []

announce(url, 1)
announce(url, 2, 0)
announce(url, 3)

# add one paused peer
announce(url, 6, event = 'paused')

peers = announce(url, 5)
if peers != ['\x01', '\x02', '\x03', '\x06'] \
	and peers != ['\x01', '\x02', '\x03', '\x05', '\x06'] \
	and peers != ['\x01', '\x02', '\x03', '\x04', '\x05', '\x06'] \
	and peers != ['\x01', '\x02', '\x03', '\x04', '\x05', '\x06']:
	errors.append('unexpected announce reponse for active downloader')

peers = announce(url, 4, 0)
if peers != ['\x01', '\x03', '\x05'] and peers != ['\x01', '\x03', '\x04', '\x05']:
	errors.append('unexpected announce reponse for seed')

response = scrape(url)
if response['downloaded'] != 0:
	errors.append('SCRAPE ERROR: %d != 0' % response['downloaded'])

if response['complete'] != 2:
	errors.append('SCRAPE ERROR: %d != 2' % response['complete'])

if response['incomplete'] != 4:
	errors.append('SCRAPE ERROR: %d != 4' % response['incomplete'])

if response['downloaders'] != 3:
	errors.append('SCRAPE ERROR: %d != 3' % response['downloaders'])




info_hash = '1' * 20

# announce 1000 seeds
for i in xrange(1, 1000):
	announce(url, i, 0)

# announce 1 downloader
announce(url, 9999)

num_peers = 0
# announce the 1000 seeds again and sum up how
# many times the single downloader is returned
for i in xrange(1, 1000):
	num_peers += len(announce(url, i, 0))

print num_peers
if num_peers > 50: errors.append('the active downloader was handed out %fx too many times' % (num_peers / 50))
if num_peers < 50: errors.append('the active downloader was handed out few many times (%fx)' % (num_peers / 50))

print scrape(url)


# test the same thing but with 999 active downloaders and a single seed
info_hash = '2' * 20

# announce 100 downloaders
for i in xrange(2, 101):
	announce(url, i)

# announce 1 seed
announce(url, 1, 0)
print scrape(url)

num_peers = 0
# announce the 100 downloaders again and sum up how
# many times the single seed is returned
for i in xrange(2, 101):
	num_peers += '\x01' in announce(url, i)

print num_peers
if num_peers != 50: errors.append('the seed was not handed out the corret number of times')

print scrape(url)

for e in errors: print 'ERROR: %s' % e

print 'average interval: %d' % (interval_sum / num_announces)
print 'max interval: %d' % max_interval
print 'min interval: %d' % min_interval

