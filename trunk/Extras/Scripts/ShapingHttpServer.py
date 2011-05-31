#!/usr/bin/env python

__version__ = "1.0"

import os
import posixpath
import BaseHTTPServer
import urllib
import urlparse
import cgi
import shutil
import mimetypes
import time
import sys

VERBOSE   = True
NEXT_TIME = -1
BANDWIDTH = 1000000 # Bits Per Second

class MediaRequestHandler(BaseHTTPServer.BaseHTTPRequestHandler):
    server_version = "MediaTest/" + __version__
        
    def do_GET(self):
        if VERBOSE: print "GET", self.path
        f = self.send_head()
        if f:
            self.copyfile(f, self.wfile)
            f.close()

    def do_HEAD(self):
        f = self.send_head()
        if f:
            f.close()

    def send_head(self):
        path = self.translate_path(self.path)
        f = None
        if os.path.isdir(path): return None
        ctype = self.guess_type(path)
        try:
            f = open(path, 'rb')
        except IOError:
            self.send_error(404, "File not found")
            return None
        self.send_response(200)
        self.send_header("Content-type", ctype)
        fs = os.fstat(f.fileno())
        self.send_header("Content-Length", str(fs[6]))
        self.send_header("Last-Modified", self.date_time_string(fs.st_mtime))
        self.end_headers()
        return f

    def translate_path(self, path):
        path = urlparse.urlparse(path)[2]
        path = posixpath.normpath(urllib.unquote(path))
        words = path.split('/')
        words = filter(None, words)
        path = os.getcwd()
        for word in words:
            drive, word = os.path.splitdrive(word)
            head, word = os.path.split(word)
            if word in (os.curdir, os.pardir): continue
            path = os.path.join(path, word)
        return path

    def copyfile(self, source, outputfile):
        global NEXT_TIME
        start = time.time()
        #print "START", start
        
        total=0
        elapsed = 0
        while True:
            chunk = source.read(4096)
            #print "READ", len(chunk)
            if len(chunk) == 0: break
            now = time.time()
            if NEXT_TIME == -1:
                NEXT_TIME = now
            #print 'NOW =',now, ' NEXT = ', NEXT_TIME
            delay = 0
            if now < NEXT_TIME:
                delay = NEXT_TIME-now
            
            NEXT_TIME += float(len(chunk)*8)/BANDWIDTH

            if delay:
                time.sleep(delay)

            try:
                outputfile.write(chunk)
            except:
                break
            total += len(chunk)
            now = time.time()
            elapsed = now-start
            rate = total/elapsed
            
            #print "STAT", total, elapsed, rate
                
        #print "END", time.time()
        #print "ELAPSED", elapsed
        print "BPS =", (8*total)/elapsed
        
    def guess_type(self, path):
        base, ext = posixpath.splitext(path)
        if ext in self.extensions_map:
            return self.extensions_map[ext]
        ext = ext.lower()
        if ext in self.extensions_map:
            return self.extensions_map[ext]
        else:
            return self.extensions_map['']

    if not mimetypes.inited:
        mimetypes.init() # try to read system mime.types
    extensions_map = mimetypes.types_map.copy()
    extensions_map.update({
        '': 'application/octet-stream', # Default
        '.py': 'text/plain',
        '.c': 'text/plain',
        '.h': 'text/plain',
        '.mp4': 'video/mp4',
        '.mp3': 'audio/mpeg'
        })


def main(HandlerClass = MediaRequestHandler,
         ServerClass = BaseHTTPServer.HTTPServer):
    BaseHTTPServer.test(HandlerClass, ServerClass)


if __name__ == '__main__':
    main()
