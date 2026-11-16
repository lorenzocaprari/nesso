# Synthetic log only. The hidden setup writes app.log with scripts/demo-log.
# On camera: keyword miss, nesso grep, nesso index, nesso search.
# Wait matches result text (No space left / Indexed), not "command\\nline",
# because the embed progress line sits between the command and the matches.
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
Set Height 480
Set Padding 8
Set Framerate 15
Set TypingSpeed 40ms

Hide
Type `mkdir -p /tmp/nesso-demo/models && cp models/model.onnx models/vocab.txt /tmp/nesso-demo/models/ && ./scripts/demo-log -o /tmp/nesso-demo/app.log`
Enter
Type `cd /tmp/nesso-demo`
Enter
Type `export PATH="/tmp/nesso-demo/bin:$PATH"`
Enter
Type `export PS1='$ '`
Enter
Type `clear`
Enter
Show

Type `grep -c 'the disk is full' app.log`
Enter
Wait+Screen@5s /0/
Sleep 400ms

Type `nesso grep "the disk is full" app.log`
Enter
Wait+Screen@30s /No space left on device/
Sleep 1200ms

Type `nesso index -o app.nesso app.log`
Enter
Wait+Screen@120s /Indexed 5000 chunks into 'app.nesso'./
Sleep 400ms

Type `nesso search "the disk is full" -i app.nesso`
Enter
Wait+Screen@30s /No space left on device/
Sleep 2500ms
