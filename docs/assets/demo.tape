# Synthetic log only. The hidden setup writes app.log with scripts/demo-log.
# stderr is taken off the tty before the visible commands so the embed progress
# line, which includes a per-second count, is not recorded.
# Run vhs from the repository root, with the Release nesso at /tmp/nesso-demo/bin
# and the MiniLM files in models/.

Output docs/assets/demo.gif

Require grep
Require python3
Require nesso

Set Shell "bash"
Set FontFamily "JetBrains Mono"
Set FontSize 16
Set LineHeight 1.2
Set Width 1200
Set Height 400
Set Padding 8
Set Framerate 6
Set TypingSpeed 30ms

Hide
Type `mkdir -p /tmp/nesso-demo/models && cp models/model.onnx models/vocab.txt /tmp/nesso-demo/models/ && ./scripts/demo-log -o /tmp/nesso-demo/app.log`
Enter
Type `cd /tmp/nesso-demo`
Enter
Type `export PATH="/tmp/nesso-demo/bin:$PATH"`
Enter
Type `export PS1='$ '`
Enter
Type `exec 2> >(cat >&2)`
Enter
Type `clear`
Enter
Show

Type `grep -c 'the disk is full' app.log`
Enter
Wait+Screen@5s /0/
Sleep 300ms

Type `nesso grep "the disk is full" app.log -k 3`
Enter
Wait+Screen@120s /nesso grep "the disk is full" app\.log -k 3\nline /
Sleep 300ms

Type `nesso index -o app.nesso app.log`
Enter
Wait+Screen@120s /Indexed 5000 chunks into 'app.nesso'./
Sleep 300ms

Type `nesso search "the disk is full" -i app.nesso -k 3`
Enter
Wait+Screen@30s /nesso search "the disk is full" -i app\.nesso -k 3\nline /
Sleep 800ms
