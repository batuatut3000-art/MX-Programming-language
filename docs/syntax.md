# MXPL syntax

```mx
[uefi]
mouse(dpi=1600)
rect class"button"(10%,20%,25%,12%,rgb(30,100,255),radius(20%))
text class"label"(12%,18%,"Hello",rgb(255,255,255))
while True:
    Command = poll_input("KAKA> ")
    if click("button"):
        animate("button",20%,1000ms)
```

Indent blocks with four spaces. Positions can use pixels (`px`, `x`, `y`) or screen percentages (`%`). `mouse(dpi=...)` changes cursor speed. `browser("http://example.com/")` opens the small firmware browser; its JavaScript support is limited. `runbin("path")` executes an x86-64 flat binary in the UEFI process and should be used only with trusted code.

See `README.md` for the detailed syntax and runtime limitations.
