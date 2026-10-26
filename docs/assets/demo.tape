# Synthetic log only. Generated with seed 7 under /tmp; the generator is not in this repo.
# stderr is taken off the tty before the visible commands so the embed progress
# line, which includes a per-second count, is not recorded.

Output docs/assets/demo.gif

Require grep
Require nesso

Set Shell "bash"
Set FontFamily "JetBrains Mono"
Set FontSize 16
Set LineHeight 1.2
Set Width 980
Set Height 340
Set Padding 8
Set Framerate 6
Set TypingSpeed 30ms

Hide
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

Type `nesso index -o app.nesso app.log`
Enter
Wait+Screen@45s /Indexed 5000 chunks into 'app.nesso'./
Sleep 300ms

Type `nesso search "the disk is full" -i app.nesso -k 3`
Enter
Wait+Screen@20s /0\.4322/
Sleep 800ms
