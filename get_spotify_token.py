#!/usr/bin/env python3
"""
Spotify Token Getter
Run this once to get your refresh token for the Pico W
"""

from dotenv import load_dotenv
import os
import requests
import base64
from http.server import HTTPServer, BaseHTTPRequestHandler
import webbrowser
from urllib.parse import urlparse, parse_qs

load_dotenv()

# === CONFIGURATION - FILL THESE IN ===
CLIENT_ID = os.environ.get("SPOTIFY_CLIENT_ID")
CLIENT_SECRET = os.environ.get("SPOTIFY_CLIENT_SECRET")
REDIRECT_URI = "http://127.0.0.1:3000/callback"
SCOPE = "user-read-currently-playing user-read-playback-state user-modify-playback-state"
# =====================================

auth_code = None

class CallbackHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        global auth_code

        print(f"Received request: {self.path}")

        query = urlparse(self.path).query
        params = parse_qs(query)

        if 'code' in params:
            auth_code = params['code'][0]
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            self.wfile.write(b'<html><body><h1>Success!</h1><p>You can close this window and return to the terminal.</p></body></html>')
        else:
            self.send_response(200)
            self.end_headers()

    def log_message(self, format, *args):
        pass

def get_refresh_token():
    auth_url = f"https://accounts.spotify.com/authorize?client_id={CLIENT_ID}&response_type=code&redirect_uri={REDIRECT_URI}&scope={SCOPE}"

    print("=" * 60)
    print("SPOTIFY TOKEN GETTER")
    print("=" * 60)
    print("\n1. Opening browser for Spotify authorization...")
    print("2. Log in and authorize the application")
    print("3. You'll be redirected back here automatically\n")

    webbrowser.open(auth_url)

    print("\nStarting local server on http://127.0.0.1:3000...")
    try:
        server = HTTPServer(('127.0.0.1', 3000), CallbackHandler)
        print("✓ Server started successfully!")
        print(f"Server is listening at: http://127.0.0.1:3000/callback")
        print("Waiting for authorization...")
    except OSError as e:
        print(f"❌ Failed to start server: {e}")
        print("Port 8888 might be in use. Try closing other applications.")
        return

    while auth_code is None:
        print(".", end="", flush=True)  # Show we're waiting
        server.handle_request()

    print("✓ Authorization code received!")

    print("\nExchanging code for tokens...")

    auth_header = base64.b64encode(f"{CLIENT_ID}:{CLIENT_SECRET}".encode()).decode()

    response = requests.post(
        "https://accounts.spotify.com/api/token",
        headers={
            "Authorization": f"Basic {auth_header}",
            "Content-Type": "application/x-www-form-urlencoded"
        },
        data={
            "grant_type": "authorization_code",
            "code": auth_code,
            "redirect_uri": REDIRECT_URI
        }
    )

    if response.status_code == 200:
        tokens = response.json()
        print("\n" + "=" * 60)
        print("SUCCESS! Here are your tokens:")
        print("=" * 60)
        print(f"\nACCESS TOKEN (expires in 1 hour):")
        print(tokens['access_token'])
        print(f"\nREFRESH TOKEN (use this in your Pico code):")
        print(tokens['refresh_token'])
        print("\n" + "=" * 60)
        print("\nIMPORTANT: Copy the REFRESH TOKEN above.")
        print("You'll need to paste it into your Pico W code.")
        print("=" * 60)

        with open("spotify_tokens.txt", "w") as f:
            f.write(f"CLIENT_ID={CLIENT_ID}\n")
            f.write(f"CLIENT_SECRET={CLIENT_SECRET}\n")
            f.write(f"REFRESH_TOKEN={tokens['refresh_token']}\n")
            f.write(f"ACCESS_TOKEN={tokens['access_token']}\n")

        print("\n✓ Tokens saved to spotify_tokens.txt")
    else:
        print(f"\n❌ Error getting tokens: {response.status_code}")
        print(response.text)

if __name__ == "__main__":
    if CLIENT_ID == "YOUR_CLIENT_ID_HERE":
        print("ERROR: Please edit this file and add your Spotify CLIENT_ID and CLIENT_SECRET")
        print("\nGet them from: https://developer.spotify.com/dashboard")
        exit(1)

    get_refresh_token()
