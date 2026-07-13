// Beagle Smalltalk Copyright (c) 2025 Simberon Incorporated

var webAddress = "ws://127.0.0.1:4000"

var connection;
var globals;
var richTextSelectionStart;
var richTextSelectionExtent;
var richTextSelectionEnd;
var richTextSelectedString;

function enableTabsInTextArea(areaName)
{
	document.getElementById(areaName).addEventListener("keydown", function(e) {
		if (e.key == "Tab") {
			e.preventDefault();
			var start = this.selectionStart;
			var end = this.selectionEnd;

			this.value = this.value.substring(0, start) +
			"\t" + this.value.substring(end);

			this.selectionStart = start + 1;
			this.selectionEnd = start + 1;
			}
		});
}

function enableTabsInRichTextArea(areaName)
{
	document.getElementById(areaName).addEventListener("keydown", function(e) {
		if ((e.key == "Tab") | (e.key == "Enter")) {
			e.preventDefault();
			sel = window.getSelection();
			if (sel.getRangeAt && sel.rangeCount) {
				range = sel.getRangeAt(0);
				range.deleteContents();

				var el = document.createElement("div");
				if (e.key == "Tab")
					el.innerHTML = '\t';
				if (e.key == "Enter")
					el.innerHTML = '\r\n';
				var frag = document.createDocumentFragment(), node, lastNode;
				while ( (node = el.firstChild) ) {
					lastNode = frag.appendChild(node);
				}
				range.insertNode(frag);
            
				// Preserve the selection
				if (lastNode) {
					range = range.cloneRange();
					range.setStartAfter(lastNode);
					range.collapse(true);
					sel.removeAllRanges();
					sel.addRange(range);
				}
			}
		}
		});
}

function enableEnterInArea(receiver, areaName, widgetId, selector, requestedValues)
{
	document.getElementById(areaName).addEventListener("keydown", function(e) {
		if (e.key == "Enter") {
			e.preventDefault();
		requestedFieldsCallback (receiver, null, widgetId, selector, requestedValues, null);
		}
	});
}

function captureKeyInArea(receiver, keyName, areaName, widgetId, selector, requestedValues)
{
	document.getElementById(areaName).addEventListener("keydown", function(e) {
		if (e.key == keyName) {
			e.preventDefault();
		requestedFieldsCallback (receiver, null, widgetId, selector, requestedValues, e.key);
		}
	});
}

function captureControlKeyInArea(receiver, keyName, areaName, widgetId, selector, requestedValues)
{
	document.getElementById(areaName).addEventListener("keydown", function(e) {
		if (e.ctrlKey && e.key == keyName) {
			e.preventDefault();
		requestedFieldsCallback (receiver, null, widgetId, selector, requestedValues, e.key);
		}
	});
}

function connectToVM()
{
	document.getElementById("result").value = "Connecting...";
	globals = {};

	connection = new WebSocket(webAddress);
	connection.onopen = function(evt) {
		document.getElementById("result").value = "Connected!! ";
		enableTabsInTextArea("workspace");
		}
	connection.onclose = function(evt) {
		document.getElementById("result").value = "Connection closed";
	}
	connection.onmessage = function(evt) { 
		if (evt.data.charAt(0) == "~") {
			screen.runCommands(JSON.parse(evt.data.substring(1, evt.data.length)));
		}
		else if (evt.data.charAt(0) == "`") {
			eval(evt.data.substring(1, evt.data.length));
		}
		else
			document.getElementById("result").value = evt.data;
	}
	connection.onerror = function(event) {
		document.getElementById("result").value = event.type;
	}
}

function over() {
	document.getElementById("result").value = "";
	connection.send("step");
}

function into() {
	document.getElementById("result").value = "";
	connection.send("send");
}

function peek() {
	document.getElementById("result").value = "";
	connection.send("peek");
}

function run() {
	document.getElementById("result").value = "";
	connection.send("run");
}

function stack() {
	document.getElementById("result").value = "";
	connection.send("stack " + document.getElementById("workspace").value);
}

function spaces() {
	document.getElementById("result").value = "";
	connection.send("spaces");
}

function show() {
	document.getElementById("result").value = "";
	connection.send("inspect " + document.getElementById("workspace").value);
}

function dump() {
	document.getElementById("result").value = "";
	connection.send("dump " + document.getElementById("workspace").value);
}

function closeSmalltalk() {
	document.getElementById("result").value = "";
	connection.send("close");
}

function shutdownSmalltalk() {
	document.getElementById("result").value = "";
	connection.send("shutdown");
}


function context() {
	document.getElementById("result").value = "";
	connection.send("context");
}

function doIt() {
	document.getElementById("result").value = "";
	connection.send("doit " + document.getElementById("workspace").value);
}

function printIt() {
	document.getElementById("result").value = "";
	connection.send("printit " + document.getElementById("workspace").value);
}

function renderIt() {
	document.getElementById("result").value = "";
	connection.send("renderit " + document.getElementById("workspace").value);
}

function debugIt() {
	document.getElementById("result").value = "";
	connection.send("debugit " + document.getElementById("workspace").value);
}

function fileIn() {
	document.getElementById("result").value = "";
	connection.send("filein " + document.getElementById("workspace").value);
}

function showString() {
	document.getElementById("result").value = "";
	connection.send("showstring " + document.getElementById("workspace").value);
}

function openCodeBrowser() {
	connection.send("doit CodeBrowser open");
}

function openWorkspace() {
	connection.send("doit Workspace open");
}

function browseSenders() {
	connection.send("doit BeagleSystem browseSenders");
}

function browseImplementers() {
	connection.send("doit BeagleSystem browseImplementers");
}

function browseClassReferences() {
	connection.send("doit BeagleSystem browseClassReferences");
}

function saveImage() {
	connection.send("doit BeagleSystem saveImage");
}

function garbageCollect() {
	connection.send("doit BeagleSystem globalGarbageCollect");
}

function fileoutSources() {
	document.getElementById("result").value = "";
	connection.send("doit KitManager current fileoutAllKits");
}

function fileinSources() {
	document.getElementById("result").value = "";
	connection.send("doit KitManager current fileinAllKits");
}

function stringArtWindow() {
	connection.send("doit StringArtWindow open");
}


function simTalkCallback0 (receiver, message) {
	connection.send(
		"evaluateJSON " + `[[["` + receiver + `" , "value"] ,"` + message + `"]]`
	);
}

function simTalkCallback1 (receiver, message, arg) {
	connection.send(
		"evaluateJSON " + `[[["` + receiver + `" , "value"] ,"` + message + `","` + arg + `"]]`
	);
}

function simTalkCallbackJSON1 (receiver, message, arg) {
	connection.send(
		"evaluateJSON " + `[[["` + receiver + `" , "value"] ,"` + message + `",` + arg + `]]`
	);
}

function simTalkCallbackJSON1_1 (receiver, message, arg1, arg2) {
	connection.send(
		"evaluateJSON " + `[[["` + receiver + `" , "value"] ,"` + message + `",` + arg1 + `,"` + arg2 + `"]]`
	);
}

function simTalkCallback2 (receiver, message, arg1, arg2) {
	connection.send(
		"evaluateJSON " + `[[["` + receiver + `" , "value"] ,"` + message + `","` + arg1 + `","` + arg2 + `"]]`
	);
}

function simTalkCallback3 (receiver, message, arg1, arg2, arg3) {
	connection.send(
		"evaluateJSON " + `[[["` + receiver + `" , "value"] ,"` + message + `","` + arg1 + `","` + arg2 + `","` + arg3 + `"]]`
	);
}

function textBoxSelectedString (widgetId) {
	var element = document.getElementById(widgetId);
	var selectionStart = element.selectionStart;
	var selectionEnd = element.selectionEnd;
	return element.value.substring(selectionStart, selectionEnd);
}

function getTextNodesIn(node) {
  let textNodes = [];
  function recurse(currentNode) {
	if (currentNode !== null) {
		if (currentNode.nodeType === Node.TEXT_NODE) {
			textNodes.push(currentNode);
		} else {
		for (let child of currentNode.childNodes) {
			recurse(child);
			}
		}
	}
  }
  recurse(node);
  return textNodes;
}

function getAbsoluteOffsets(container) {
  const selection = window.getSelection();
  if (!selection.rangeCount) return null;

  const range = selection.getRangeAt(0);
  const textNodes = getTextNodesIn(container);

  let start = 0;
  let end = 0;
  let foundStart = false;
  let reachedEnd = false;
  let totalOffset = 0;

  for (const node of textNodes) {
    const nodeLength = node.textContent.length;

    // If we haven't found the start node yet
    if (!foundStart) {
      if (node === range.startContainer) {
        start = totalOffset + range.startOffset;
        foundStart = true;
      }
    }

    // If we've found the start but not yet the end
    if (foundStart && !reachedEnd) {
      if (node === range.endContainer) {
        end = totalOffset + range.endOffset;
        reachedEnd = true;
        break;
      }
    }

    totalOffset += nodeLength;
  }

  // If selection is collapsed (caret), end = start
  if (!reachedEnd) {
    end = start;
  }

  return { start, end, text: range.toString() };
}
function captureRichTextSelection (elementId) {
	const element = document.getElementById(elementId);
	const offsets = getAbsoluteOffsets(element);
	
	if (offsets) {
		richTextSelectedString = offsets.text;
		richTextSelectionStart = offsets.start;
		richTextSelectionEnd = offsets.end;
	}
}

function insertText (widgetId, string, offset) {
	var element = document.getElementById(widgetId);
	element.value = element.value.substring(0, offset) + string + element.value.substring(offset, offset + element.value.length);
	element.focus();
	element.setSelectionRange(offset, offset + string.length);
}

function insertRichText(divId, text, offset) {
    const div = document.getElementById(divId);
    if (!div || !div.isContentEditable) {
        console.error("Element is not a contenteditable div");
        return;
    }

    // Flatten all text nodes in order
    function getTextNodes(node) {
        let nodes = [];
        for (let child of node.childNodes) {
            if (child.nodeType === Node.TEXT_NODE) {
                nodes.push(child);
            } else {
                nodes = nodes.concat(getTextNodes(child));
            }
        }
        return nodes;
    }

    const textNodes = getTextNodes(div);

    let currentOffset = 0;
    for (let node of textNodes) {
        const nodeLength = node.textContent.length;

        if (offset <= currentOffset + nodeLength) {
            // Found insertion point in this node
            const localOffset = offset - currentOffset;
            const before = node.textContent.slice(0, localOffset);
            const after = node.textContent.slice(localOffset);

            const newNodes = [
                document.createTextNode(before),
                document.createTextNode(text),
                document.createTextNode(after)
            ];

            const parent = node.parentNode;
            parent.insertBefore(newNodes[0], node);
            parent.insertBefore(newNodes[1], node);
            parent.insertBefore(newNodes[2], node);
            parent.removeChild(node);

            return;
        }

        currentOffset += nodeLength;
    }

    // If offset is past the end, append text
    div.appendChild(document.createTextNode(text));
}


function setSelectionByAbsoluteOffsets(widgetId, startOffset, endOffset) {
	const container = document.getElementById(widgetId);
    const range = document.createRange();
    let currentOffset = 0;
    let startNode, startNodeOffset, endNode, endNodeOffset;

    function recurse(node) {
        if (node.nodeType === Node.TEXT_NODE) {
            const textLength = node.textContent.length;

            // Start
            if (!startNode && currentOffset + textLength >= startOffset) {
                startNode = node;
                startNodeOffset = startOffset - currentOffset;
            }

            // End
            if (!endNode && currentOffset + textLength >= endOffset) {
                endNode = node;
                endNodeOffset = endOffset - currentOffset;
            }

            currentOffset += textLength;
        } else if (node.nodeType === Node.ELEMENT_NODE) {
            for (let child of node.childNodes) {
                recurse(child);
                if (endNode) break; // Stop early if both points found
            }
        }
    }

    recurse(container);

    if (startNode && endNode) {
        range.setStart(startNode, startNodeOffset);
        range.setEnd(endNode, endNodeOffset);

        const selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
    }
}

function requestedFieldsCallback (receiver, menuId, widgetId, selector, requestedValues, mouseEvent) {
	var map = {};
	var widget = document.getElementById(widgetId);

	map['widgetId'] = widgetId;
	map['menuId'] = menuId;

	for (let i=0; i < requestedValues.length; i++) {
		if (requestedValues[i] == 'value') {map['value'] = widget.value;};
		if (requestedValues[i] == 'innerHTML') {map['innerHTML'] = widget.innerHTML;};
		if (requestedValues[i] == 'selectionIndex') {map['selectionIndex'] = widget.selectionIndex;};
		if (requestedValues[i] == 'selectionStart') {map['selectionStart'] = widget.selectionStart;};
		if (requestedValues[i] == 'selectionEnd') {map['selectionEnd'] = widget.selectionEnd;};
		if (requestedValues[i] == 'selectedString') {map['selectedString'] = textBoxSelectedString(widgetId);};
		if (requestedValues[i] == 'richTextSelectionStart') {captureRichTextSelection (widgetId); map['richTextSelectionStart'] = richTextSelectionStart;};
		if (requestedValues[i] == 'richTextSelectionEnd') {captureRichTextSelection (widgetId); map['richTextSelectionEnd'] = richTextSelectionEnd;};
		if (requestedValues[i] == 'richTextSelectedString') {captureRichTextSelection (widgetId); map['richTextSelectedString'] = richTextSelectedString;};
		if (mouseEvent != null) {
			if (requestedValues[i] == 'pageX') {map['pageX'] = mouseEvent.pageX;};
			if (requestedValues[i] == 'pageY') {map['pageY'] = mouseEvent.pageY;};
		}
	}
	connection.send(
		"evaluateJSON " + `[[["` + receiver + `" , "value"] ,"` + selector + `",` + JSON.stringify(map) + `]]`
	);
}

function requestedFieldsCallbackNoCapture (receiver, menuId, widgetId, selector, requestedValues, mouseEvent) {
	var map = {};
	var widget = document.getElementById(widgetId);

	map['widgetId'] = widgetId;
	map['menuId'] = menuId;

	for (let i=0; i < requestedValues.length; i++) {
		if (requestedValues[i] == 'value') {map['value'] = widget.value;};
		if (requestedValues[i] == 'innerHTML') {map['innerHTML'] = widget.innerHTML;};
		if (requestedValues[i] == 'selectionIndex') {map['selectionIndex'] = widget.selectionIndex;};
		if (requestedValues[i] == 'selectionStart') {map['selectionStart'] = widget.selectionStart;};
		if (requestedValues[i] == 'selectionEnd') {map['selectionEnd'] = widget.selectionEnd;};
		if (requestedValues[i] == 'selectedString') {map['selectedString'] = textBoxSelectedString(widgetId);};
		if (requestedValues[i] == 'richTextSelectionStart') {map['richTextSelectionStart'] = richTextSelectionStart;};
		if (requestedValues[i] == 'richTextSelectionEnd') {map['richTextSelectionEnd'] = richTextSelectionEnd;};
		if (requestedValues[i] == 'richTextSelectedString') {map['richTextSelectedString'] = richTextSelectedString;};
		if (mouseEvent != null) {
			if (requestedValues[i] == 'pageX') {map['pageX'] = mouseEvent.pageX;};
			if (requestedValues[i] == 'pageY') {map['pageY'] = mouseEvent.pageY;};
		}
	}
	connection.send(
		"evaluateJSON " + `[[["` + receiver + `" , "value"] ,"` + selector + `",` + JSON.stringify(map) + `]]`
	);
}

function measureTextOnCanvas(text, font, receiver, selector) {
  const canvas = document.createElement('canvas');
  const context = canvas.getContext('2d');
  context.font = font; // e.g., "16px Arial"

  const metrics = context.measureText(text);
  const width = metrics.width;

  const height = metrics.actualBoundingBoxAscent + metrics.actualBoundingBoxDescent; 

  connection.send (
		"evaluateJSON " + `[[["` + receiver + `" , "value"] ,"` + selector + `",` + JSON.stringify({ width, height }) + `]]`);
}
