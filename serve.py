import http.server
import socketserver

class CORSHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        super().end_headers()

with socketserver.TCPServer(("", 8000), CORSHTTPRequestHandler) as httpd:
    print("Serving at port 8000")
    httpd.serve_forever()
