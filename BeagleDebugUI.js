
var connection;
/*
var screen;

class Command {
	constructor(name, arguments) {
		this.name = name;
		this.arguments = arguments;
	}

	runOn(uiContext) {
		switch (this.name) {
			case "fillStyle":
				uiContext.fillStyle(this.arguments[1]);
				break;
			
		}
	}
}

class Component {
	constructor(name, operators) {
		this.name = name;
		this.operators = operators;
	}

	setOperators(newOperators) {
		this.operators = newOperators;
	}

	renderOn(uiContext) {
		for (const operator of operators){
			operator.runOn(uiContext);
		}
	}

}


class Screen {
	constructor() {
		this.components = new Map();
	}

	addComponent(component) {
		var component = new Component(component.name);
		
		screen.set(component.name, component);
	}

	updateComponent(component) {
		screen.get(component.name).setCommands(component.arguments);
	}

	renderOn(uiContext) {
	}
}

function runCommand(command) {
	switch(command[0]) {
		case "create":
			screen.addComponent(command[1]);
			break;

		case "update":
			screen.updateComponent(command[1]);
			break;

		case "delete":
			screen.deleteComponent(command[1]);
			break;

		case "bringToFront":
			screen.bringToFrontComponent(command[1]);
			break;
	}
}

function runCommands(commands) {
	for (const command of commands){
		run(command);
	}
}
*/

function drawScreen (commands) {
		var c = document.getElementById("uiCanvas");
		var uiContext = c.getContext("2d");
		uiContext.fillStyle = "#E0E0E0";
		uiContext.fillRect(0, 0, 800, 600);
		
		uiContext.fillStyle = "#FFFFFF";
		uiContext.fillRect(40, 40, 300, 200);
		uiContext.strokeStyle = "#000000";
		uiContext.strokeRect(40, 40, 300, 200);
		uiContext.fillStyle = "#000000";
		uiContext.font = "16px Arial";
		uiContext.fillText(commands, 60,60);
}

function connectToVM()
{
	document.getElementById("result").value = "Connecting...";
//	screen = new Screen();
	connection = new WebSocket("ws://localhost:4100");
	connection.onopen = function(evt) {
		document.getElementById("result").value = "Connected!! ";
		document.getElementById("workspace").addEventListener("keydown", function(e) {
			if (e.key == "Tab") {
				e.preventDefault();
				var start = this.selectionStart;
				var end = this.selectionEnd;

				// set textarea value to: text before caret + tab + text after caret
				this.value = this.value.substring(0, start) +
				"\t" + this.value.substring(end);

				// put caret at right position again
				this.selectionStart =
				this.selectionEnd = start + 1;
				}
			});
		var c = document.getElementById("uiCanvas");
		var uiContext = c.getContext("2d");
		uiContext.fillStyle = "#E0E0E0";
		uiContext.fillRect(0, 0, 800, 600);
		}
	connection.onclose = function(evt) {
		document.getElementById("result").value = "Connection closed";
	}
	connection.onmessage = function(evt) {
		document.getElementById("result").value = evt.data;
	}
	connection.onerror = function(event) {
		document.getElementById("result").value = event.type;
	}
}

function over() {
	connection.send("step");
}

function into() {
	connection.send("send");
}

function peek() {
	connection.send("peek");
}

function run() {
	connection.send("run");
}

function stack() {
	connection.send("stack " + document.getElementById("workspace").value);
}

function spaces() {
	connection.send("spaces");
}

function show() {
	connection.send("inspect " + document.getElementById("workspace").value);
}

function dump() {
	connection.send("dump " + document.getElementById("workspace").value);
}

function closeSmalltalk() {
	connection.send("close");
}

function context() {
	connection.send("context");
}

function doIt() {
	connection.send("doit " + document.getElementById("workspace").value);
}

function printIt() {
	connection.send("printit " + document.getElementById("workspace").value);
}

function debugIt() {
	connection.send("debugit " + document.getElementById("workspace").value);
}
