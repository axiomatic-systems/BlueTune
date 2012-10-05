####################################################
#
# Simple tool to proxy incoming UDP packets to an HTTP client
#
####################################################

import argparse
import time
import struct
import socket
import sys
import BaseHTTPServer

MPEG_TS_MIME_TYPE = 'video/MP2T'

class HttpHandler(BaseHTTPServer.BaseHTTPRequestHandler):
    def do_HEAD(s):
        s.send_response(200)
        s.send_header("Content-type", MPEG_TS_MIME_TYPE)
        s.end_headers()

    def do_GET(s):
        if Options.verbose:
            print 'Client Connection'
            
        HttpHandler.do_HEAD(s)
        receive_loop(s)
  
def receive_loop(output):
    # Create a socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Allow multiple copies of this program on one machine
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # Bind it to the port
    s.bind(('', Options.udp_port))

    if Options.multicast_group:
        if Options.verbose: print 'Joining multicast Group', Options.multicast_group
        addrinfo = socket.getaddrinfo(Options.multicast_group, None)[0]
        group_bin = socket.inet_pton(socket.AF_INET, addrinfo[4][0])
        mreq = group_bin + struct.pack('=I', socket.INADDR_ANY)
        s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

    # Loop, printing any data we receive
    total = 0
    packet_count = 0
    while True:
        data, sender = s.recvfrom(32768)
        payload = data[Options.skip_header:]
        total += len(payload)
        packet_count += 1
        output.wfile.write(payload)
        if Options.verbose: 
            print "\r"+str(packet_count)+" packets, "+str(total)+" bytes",
            sys.stdout.flush()
        
def run_server():
    server_address = ('', Options.http_port)
    server = BaseHTTPServer.HTTPServer(server_address, HttpHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    server.server_close()

def main():
    parser = argparse.ArgumentParser(description='UDP to HTTP Proxy')
    parser.add_argument('--multicast-group', dest='multicast_group', help='Join a multicast group')
    parser.add_argument('--udp-port', required=True, dest='udp_port', type=int, default=0, help='UDP port number to receive on')
    parser.add_argument('--http-port', dest='http_port', type=int, default=8080, help='HTTP port number to serve on')
    parser.add_argument('--skip-header', dest='skip_header', type=int, default=0, help='Skip bytes at the start of each UDP packet')
    parser.add_argument('--verbose', dest='verbose', action='store_true', default=False, help='be verbose')

    global Options
    Options = parser.parse_args()
    if Options.verbose:
        print 'Listening on port', Options.udp_port
        print 'Serving on port', Options.http_port
    run_server()

if __name__ == "__main__":
    main()
    