  Issue 1 — The binary is still on disk after shutdown (by design, but wrong design).
  MoveFileExA(MOVEFILE_DELAY_UNTIL_REBOOT) only marks the exe for deletion when Windows next reboots —
  it never deletes the file during the current session. The user sees MicrosoftEdgeUpdate.exe still in
  C:\Program Files (x86)\Microsoft\EdgeUpdate\ and correctly interprets it as "not removed."

  Issue 2 — RemoveDirectoryA on the data dir may silently fail if ss.tmp (screenshot temp) was left
  behind by a crashed helper process. There's no cleanup of that file before the RemoveDirectory call.

  The fix: spawn a hidden PowerShell one-liner before calling ExitProcess that waits 2 seconds (for the
  process to fully exit and release the file lock) then force-deletes the binary. Keep MoveFileExA as a
  belt-and-suspenders backup. Also add ss.tmp to the pre-directory-removal cleanup.

  For these make sure no cmd prompt is visable 

  Issue 3 - make sure the file structure makes sense for our project. The folder exfil server contains the exploit-server logic, etc. Each should have their own folder, for example: Exploit-server, Exfil-server, redirector, etc. 

  