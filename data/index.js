var connection;
var debugMode = false;
var reconnectTimeout;
var currentSong;
var keepAliveInterval;

var lastTyped = 0;
var typing = false;

function sendEvent(wsEvent)
{
    var out_event = JSON.stringify(wsEvent);
    if (debugMode)
        console.log("Transmit: " + out_event);
    try
    {
        connection.send(out_event);
    } catch (err) {
        console.log(err);
    }
}

function updateUserNick(username, details, picture = "flipper.png")
{
    if (document.getElementById("nicknameField").value != username)
    {
        var id = "user_" + username;
        if (document.getElementById(id) == undefined)
        {
            var chat_list_div = document.createElement("div");
            chat_list_div.id = id;
            chat_list_div.className = "chat_list"; // also active_chat
            chat_list_div.innerHTML = "<div class=\"chat_people\"><div class=\"chat_img\">" +
                                      "<img id=\"avatar_" + id + "\" src=\"" + picture + "\"> </div>" +
                                        "<div class=\"chat_ib\" style=\"padding-top: 0px;\">" +
                                        "<div style=\"font-size: 18px;\" id=\"nick_" + id + "\">" + username + "</div>" +
                                        "<span style=\"font-size: 10px;\" id=\"details_" + id + "\">" + details + "</span>" +
                                        "</div></div>";
            document.getElementById('user_list').appendChild(chat_list_div);
        } else {
            var trow = document.getElementById("nick_" + id);
            if (trow != undefined)
            {
                trow.innerHTML = username;
            }
            var trow2 = document.getElementById("details_" + id);
            if (trow2 != undefined)
            {
                trow2.innerHTML = details;
            }
            var avatar = document.getElementById("avatar_" + id);
            if (avatar != undefined)
            {
                avatar.src = picture;
            }
        }
    }
}

function removeUserEntry(username)
{
    var id = "user_" + username;
    var trow = document.getElementById(id);
    if (trow != undefined)
    {
        document.getElementById('user_list').removeChild(trow);
    }
}

function addOutgoingMessage(text)
{
    var d = new Date();
    var dString = d.toLocaleTimeString();
    var outgoing_msg_div = document.createElement("div");
    outgoing_msg_div.className = "outgoing_msg"; // also active_chat
    outgoing_msg_div.innerHTML = "<div class=\"sent_msg\">" +
                                 "<p>" + text + "</p>" +
                                 "<span class=\"time_date\">" + dString + "</span> </div>";
    var msgHistory = document.getElementById('msg_history');
    msgHistory.appendChild(outgoing_msg_div);
    msgHistory.scrollTo(0, msgHistory.scrollHeight);

}

function addIncomingMessage(username, text, picture = "flipper.png")
{
    var d = new Date();
    var dString = d.toLocaleTimeString();
    var incoming_msg_div = document.createElement("div");
    incoming_msg_div.className = "incoming_msg"; // also active_chat
    incoming_msg_div.innerHTML = "<div class=\"incoming_msg_img\"><img src=\"" + picture + "\" alt=\"" + username + "\"></div>" +
                                 "<div class=\"received_msg\"><span style=\"font-size: 10px; color: red;\">" + username + "</span><div class=\"received_withd_msg\">" +
                                 "<p>" + text + "</p>" +
                                 "<span class=\"time_date\">" + dString + "</span> </div></div>";
    var msgHistory = document.getElementById('msg_history');
    msgHistory.appendChild(incoming_msg_div);
    msgHistory.scrollTo(0, msgHistory.scrollHeight);

}

function logIt(message)
{
    addIncomingMessage("The Reaper",message,"reaper.png");
}

function setupWebsocket()
{
    try
    {
        //logIt("Attempting to connect to Channel " + websocketChannel);
        var hostname = location.hostname;
        if (hostname == '')
            hostname = '192.168.34.246';
        var wsUrl = 'ws://' + hostname + ':81/';
        logIt("Connecting to " + wsUrl);
        connection = new WebSocket(wsUrl, ['arduino']);
        connection.onopen = function () {
            logIt("Connected to ESP32<br />To change the frequency of the chat server, type:<br />/freq 315.0 (315 mhz)");
            sendEvent({"event": "join", "username": document.getElementById("nicknameField").value});
        };

        connection.onerror = function (error) {
          logIt("WebSocket error!");
        };

        //Code for handling incoming Websocket messages from the server
        connection.onmessage = function (e) {
            if (debugMode)
                console.log("Receive: " + e.data);
            var jsonObject = JSON.parse(e.data);
            if (jsonObject.hasOwnProperty('mhz'))
            {
                document.getElementById('freq').innerHTML = jsonObject.mhz + " Mhz";
            }
            if (jsonObject.hasOwnProperty('users'))
            {
                var usersList = jsonObject.users;
                for (var i = 0; i < usersList.length; i++) 
                {
                    var user = usersList[i];
                    if (user.s == 'radio')
                    {
                        updateUserNick(user.u, "RSSI " + user.r, "flipper.png");
                    } else {
                        updateUserNick(user.u, user.s, "spy.png");
                    }
                }
            }
            if (jsonObject.hasOwnProperty('event'))
            {
                var event = jsonObject.event;
                if (event == 'chat') {
                    if (jsonObject.hasOwnProperty('text'))
                    {
                        var picture = "spy.png";
                        if (jsonObject.hasOwnProperty('source'))
                        {
                            if (jsonObject.hasOwnProperty('rssi') && jsonObject.source == "radio")
                            {
                                picture = "flipper.png";
                                updateUserNick(jsonObject.username, "RSSI " + jsonObject.rssi, "flipper.png");
                            } else {
                                updateUserNick(jsonObject.username, jsonObject.source, "spy.png");
                            }
                        }
                        addIncomingMessage(jsonObject.username, escapeHTML(jsonObject.text), picture);
                    }
                } else if (event == 'join') {
                    if (jsonObject.hasOwnProperty('source'))
                    {
                        if (jsonObject.hasOwnProperty('rssi') && jsonObject.source == "radio")
                        {
                            updateUserNick(jsonObject.username, "RSSI " + jsonObject.rssi, "flipper.png");
                        } else {
                            updateUserNick(jsonObject.username, jsonObject.source, "spy.png");
                        }
                    }
                } else if (event == 'part') {
                    removeUserEntry(jsonObject.username);
                } else if (event == 'frequency') {
                    logIt("Frequency changed to " + jsonObject.mhz + " Mhz");
                }
            }
        };

        connection.onclose = function () {
          logIt('WebSocket connection closed');
          document.getElementById('user_list').innerHTML = "";
          reconnectTimeout = setTimeout(setupWebsocket, 3000);
          clearInterval(keepAliveInterval);
        };
    } catch (err) {
        console.log(err);
    }
}

window.onload = function() {
    var a = Math.floor(100000 + Math.random() * 900000);   
    a = String(a);
    a = a.substring(0,4);
    
    var nickname = prompt('enter nickname', 'Hacker' + a);
    document.getElementById("nicknameField").value = nickname;
    setupWebsocket();
    $("#messageBox").keyup(function(event) {
        if (event.keyCode === 13) {
            $("#sendButton").click();
        }
        lastTyped = Date.now();
    });
};


function sendMessage()
{
  var msgBox = document.getElementById("messageBox");
  if (msgBox.value != "")
  {
      var text = msgBox.value;
      msgBox.value = "";
      if (text.startsWith("/freq"))
      {
          var mhz = parseFloat(text.substring(5));
          logIt("Frequency Changed to " + mhz + " Mhz");
          document.getElementById('freq').innerHTML = mhz + " Mhz";
          var mm = {"event":"frequency", "mhz": mhz, "username": document.getElementById('nicknameField').value};
          sendEvent(mm);
      } else {
          var mm = {"event":"chat", "text": text, "username": document.getElementById('nicknameField').value};
          sendEvent(mm);
          addOutgoingMessage(escapeHTML(mm.text));
      }
  }
}

var escapeHTML = function(unsafe)
{
    var result = unsafe.replace(/[&<"']/g, function(m) {
        switch (m) {
          case '&':
            return '&amp;';
          case '<':
            return '&lt;';
          case '"':
            return '&quot;';
          default:
            return '&#039;';
        }
    });
    return result;
};
