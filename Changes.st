! CodeGenerator methodsFor: 'special words' !
pushCopiedSelf
    
    | selfVariable |
    selfVariable := self selfVariable.
    selfVariable notNil ifTrue: [self pushLocalVariableReference: selfVariable]! !

! CodeGenerator methodsFor: 'special words' !
selfVariable

    | selfVariable |
    selfVariable := (self methodLocalVariableAnalysis scopeFor: nodeStack last)
                copiedVariables detect: [:eachVariable | eachVariable isSelf]
                    ifNone: [nil].
    ^selfVariable! !

! SequenceableCollection methodsFor: 'manipulation' !
atAllPut: aValue
	1 to: self size do: [ :i | self at: i put: aValue ]! !

! Integer methodsFor: 'benchmarks' !
benchFib  "Handy send-heavy benchmark"
	"(result // seconds to run) = approx calls per second"
	" | r t |
	  t := Time millisecondsToRun: [r := 26 benchFib].
	  (r * 1000) // t"
	"138000 on a Mac 8100/100"
	^ self < 2
		ifTrue: [1] 
		ifFalse: [(self - 1) benchFib + (self - 2) benchFib + 1]! !

! Integer methodsFor: 'benchmarks' !
benchmark  "Handy bytecode-heavy benchmark"
	"(500000 // time to run) = approx bytecodes per second"
	"5000000 // (Time millisecondsToRun: [10 benchmark]) * 1000"
	"3059000 on a Mac 8100/100"
    | size flags prime k count |
    size := 8190.
    1 to: self do:
        [:iter |
        count := 0.
        flags := (Array new: size) atAllPut: true.
        1 to: size do:
            [:i | (flags at: i) ifTrue:
                [prime := i+1.
                k := i + prime.
		[k <= size] whileTrue: [
			flags at: k put: false.
			k := k + prime].
                count := count + 1]]].
    ^ count! !

! Integer methodsFor: 'benchmarks' !
tinyBenchmarks
	"Report the results of running the two tiny Squeak benchmarks.
	ar 9/10/1999: Adjusted to run at least 1 sec to get more stable results"
	"0 tinyBenchmarks"
	"On a 292 MHz G3 Mac: 23,000,000 bytecodes/sec; 980,000 sends/sec"
	"On a 400 MHz PII/Win98:  18,000,000 bytecodes/sec; 1,100,000 sends/sec"
	"On a 2800 MHz i7:  1,200,000,000 bytecodes/sec; 25,000,000 sends/sec"
	"On a 2800 MHz i7 (CogVM):  1,700,000,000 bytecodes/sec; 260,000,000 sends/sec"	
	| t1 t2 r n1 n2 |
	n1 := 1.
	[t1 := Time millisecondsToRun: [n1 benchmark].
	t1 < 1000] whileTrue:[n1 := n1 * 2]. "Note: #benchmark's runtime is about O(n)"

	n2 := 28.
	[t2 := Time millisecondsToRun: [r := n2 benchFib].
	t2 < 1000] whileTrue:[n2 := n2 + 1]. 
	"Note: #benchFib's runtime is about O(k^n),
		where k is the golden number = (1 + 5 sqrt) / 2 = 1.618...."

	^ ((n1 * 500000 * 1000) // t1) asString, ' bytecodes/sec; ',
	  ((r * 1000) // t2) asString, ' sends/sec'! !

! BeagleSystem class methodsFor: 'startup' !
startAcceptSocket

    Smalltalk at: #CurrentSocketDispatcher put: SocketDispatcher new.
    (Smalltalk at: #CurrentSocketDispatcher)
        addHandler: (SocketAcceptHandler new
            portNumber: 4000;
            clientHandler: (SocketLauncher new
                screenHandlerClass: SocketUIScreenHandler;
                commandHandlerClass: DevelopmentCommandHandler;
                yourself);
            yourself);
        timeoutHandler: (SocketTimeoutHandler new
            yourself);
        startPollLoop.

! !
