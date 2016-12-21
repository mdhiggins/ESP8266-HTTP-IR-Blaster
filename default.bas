dim irButtons(6) as string
dim irCodes(6) as string
dim pword as string
pword = "password"
pass =  ""
x = 0

ir.recv.setup(5)
ir.send.setup(4)
IRBRANCH [received]
msgbranch [codebranch]

for x = 1 to 6
    irButtons(x) = read("irbut" & str(x))
    irCodes(x)   = read("ircode" & str(x))
    if irButtons(x) = "" then irButtons(x) = "UNUSED"
next x

[top]
cls
if pword == "" then
    goto [load]
    wait
else
    Print "Enter password"
    passwordbox pass
    button "Submit", [log]
    wait
end if

[log]
if pword == pass then goto [load]
wait

[load]
cls
cssclass "button", "background-color: powderblue;height: 20%;width: 30%;"
Print "ESP8266 Basic Learning IR remote"
print "Last IR recvd"
textbox ircode
print "Text for button"
textbox newtxt
print "Button number"
dropdown x, "1,2,3,4,5,6"
Button "Program Code to button", [program]
print

button irButtons(1), [bu1]
button irButtons(2), [bu2]
button irButtons(3), [bu3]
print
button irButtons(4), [bu4]
button irButtons(5), [bu5]
button irButtons(6), [bu6]

IRBRANCH [received]
wait

[received]
ircode = ir.recv.full()
return

[bu1]
ir.send(irCodes(1))
x = 1
wait

[bu2]
ir.send(irCodes(2))
x = 2
wait

[bu3]
ir.send(irCodes(3))
x = 3
wait

[bu4]
ir.send(irCodes(4))
x = 4
wait

[bu5]
ir.send(irCodes(5))
x = 5
wait

[bu6]
ir.send(irCodes(6))
x = 6
wait

[program]
irButtons(x) = newtxt
irCodes(x)  =  ircode
write("irbut" & str(x),irButtons(x) )
write("ircode" & str(x),irCodes(x))
goto [top]

[codebranch]
pass = msgget("pass")
if pass <> pword then wait
codeVar = msgget("code")
repeatVar = msgget("repeat")
pulseVar = msgget("pulse")
pDelayVar = msgget("pdelay")
rDelayVar = msgget("rdelay")
let myReturnMsg = "Triggering code " & codeVar
msgreturn myReturnMsg

if repeatVar == "" then repeatVar = "1"
if pulseVar == "" then pulseVar = "1"
if pDelayVar == "" then pDelayVar = "10"
if rDelayVar == "" then rDelayVar = "1000"
if (len(codeVar) = 1) then codeVar = irCodes(val(codeVar))

for z = 1 to val(repeatVar)
    for y = 1 to val(pulseVar)
        ir.send(codeVar)
        delay val(pDelayVar)
    next y
    delay val(rDelayVar)
next z
wait
