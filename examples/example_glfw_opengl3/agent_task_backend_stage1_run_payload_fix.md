### Task: Allow Larger Run Export Payloads Through Nginx

**Why**  
Desktop exports send every fold inline in a single JSON body. We confirmed the request constructed on the laptop contains all 312 folds (the pending SQL spool has every fold), but once it reaches the Drogon service only ~130 folds remain. Drogon happily inserts whatever folds it receives, so the only place the payload can be truncated is the nginx reverse proxy that fronts the API.

**What to change (server: agenticresearch.info)**

1. Edit `/etc/nginx/sites-available/agenticresearch.info.conf` inside the `server` block that proxies `/api/*` to Drogon.
   - Set an explicit `client_max_body_size 128m;`
   - Disable request buffering so big JSON bodies stream straight to Drogon:
     ```nginx
     proxy_request_buffering off;
     proxy_buffering off;
     proxy_max_temp_file_size 0;
     ```
   - Keep the long `proxy_read_timeout`/`proxy_send_timeout 86400;` lines that already exist.
   - Ensure `proxy_http_version 1.1;` and `proxy_set_header Connection "";` (or `proxy_set_header Connection "keep-alive";`) so nginx doesn’t split uploads across connections.

2. Reload nginx: `sudo systemctl reload nginx`

3. Verify: `curl -X POST https://agenticresearch.info/api/runs ... --data @large_run.json` using the saved payload from `/root/stage1_3_extracted/` to confirm 312 folds make it to Postgres (query `SELECT COUNT(*) FROM walkforward_folds WHERE run_id='...'`).

4. If any additional limit trips, bump Drogon’s `client_max_memory_body_size` in `/opt/stage1-drogon/config.json` (or wherever the live config resides) from `10M` to `64M` and restart the service.

**Acceptance criteria**
- Desktop export of the 312-fold run succeeds once and a subsequent `SELECT COUNT(*) FROM walkforward_folds WHERE run_id='<run>'` returns 312.
- nginx error log no longer shows `client intended to send too large body`.
