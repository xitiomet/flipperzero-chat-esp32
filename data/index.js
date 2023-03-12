var connection;
var debugMode = false;
var reconnectTimeout;
var currentSong;
var keepAliveInterval;

var lastTyped = 0;
var typing = false;

function setCookie(cname, cvalue, exdays) 
{
  const d = new Date();
  d.setTime(d.getTime() + (exdays*24*60*60*1000));
  let expires = "expires="+ d.toUTCString();
  document.cookie = cname + "=" + cvalue + ";" + expires + ";path=/";
}

function getCookie(cname) 
{
  let name = cname + "=";
  let decodedCookie = decodeURIComponent(document.cookie);
  let ca = decodedCookie.split(';');
  for(let i = 0; i <ca.length; i++) {
    let c = ca[i];
    while (c.charAt(0) == ' ') {
      c = c.substring(1);
    }
    if (c.indexOf(name) == 0) {
      return c.substring(name.length, c.length);
    }
  }
  return null;
}

function avatarFromSource(source)
{
    if (source == "flipper")
      return "flipper.png";
    else if (source.startsWith("irc"))
      return "irc.png";
    else
      return "spy.png";
}

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

function cleanDetails(details)
{
    return details.replace('_', ' ').toUpperCase();
}

function addUserEntry(username, details, picture = "spy.png")
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
                                        "<span style=\"font-size: 10px;\" id=\"details_" + id + "\">" + cleanDetails(details) + "</span>" +
                                        "</div></div>";
            document.getElementById('user_list').appendChild(chat_list_div);
        }
    }
}

function updateUserNick(username, details, picture = "spy.png")
{
    if (document.getElementById("nicknameField").value != username)
    {
        var id = "user_" + username;
        if (document.getElementById(id) == undefined)
        {
            console.log("couldnt find element: " + id);
        } else {
            var trow = document.getElementById("nick_" + id);
            if (trow != undefined)
            {
                trow.innerHTML = username;
            }
            var trow2 = document.getElementById("details_" + id);
            if (trow2 != undefined)
            {
                trow2.innerHTML = cleanDetails(details);
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

function addIncomingMessage(username, text, picture = "spy.png", timestamp = null)
{
    var d = new Date();
    if (timestamp != null)
        d = new Date(timestamp * 1000);
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
            hostname = '192.168.34.106';
        var wsUrl = 'ws://' + hostname + ':81/';
        logIt("Connecting to " + wsUrl);
        connection = new WebSocket(wsUrl, ['arduino']);
        connection.onopen = function () {
            logIt("Connected to ESP32<br />To change the frequency of the chat server, type:<br />/freq 315.0 (315 mhz)");
            sendEvent({"event": "join", "username": document.getElementById("nicknameField").value, "utc": Math.floor(Date.now() / 1000)});
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
                    if (user.s == 'flipper' || user.s == 'radio')
                    {
                        addUserEntry(user.u, "RSSI " + user.r, avatarFromSource(user.s));
                    } else {
                        addUserEntry(user.u, user.s, avatarFromSource(user.s));
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
                            picture = avatarFromSource(jsonObject.source);
                            if (jsonObject.hasOwnProperty('rssi') && (jsonObject.source == 'flipper' || jsonObject.source == 'radio'))
                            {
                                updateUserNick(jsonObject.username, "RSSI " + jsonObject.rssi, picture);
                            } else {
                                updateUserNick(jsonObject.username, jsonObject.source, picture);
                            }
                        }
                        var timestamp = null;
                        if (jsonObject.hasOwnProperty('utc'))
                        {
                            timestamp = jsonObject.utc;
                        }
                        addIncomingMessage(jsonObject.username, escapeHTML(jsonObject.text), picture, timestamp);
                    }
                } else if (event == 'info') {
                    if (jsonObject.hasOwnProperty('text'))
                    {
                        var timestamp = null;
                        if (jsonObject.hasOwnProperty('utc'))
                        {
                            timestamp = jsonObject.utc;
                        }
                        addIncomingMessage("The Reaper",escapeHTML(jsonObject.text),"reaper.png", timestamp);
                    }
                } else if (event == 'join') {
                    if (jsonObject.hasOwnProperty('source'))
                    {
                        if (jsonObject.hasOwnProperty('rssi') && (jsonObject.source == 'flipper' || jsonObject.source == 'radio'))
                        {
                            addUserEntry(jsonObject.username, "RSSI " + jsonObject.rssi, avatarFromSource(jsonObject.source));
                        } else {
                            addUserEntry(jsonObject.username, jsonObject.source, avatarFromSource(jsonObject.source));
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
    var savedNickname = getCookie("username");
    if (savedNickname == null)
    {
        var a = Math.floor(100000 + Math.random() * 900000);   
        a = String(a);
        a = a.substring(0,4);
        var suggested =  'Hacker' + a;
        var nickname = prompt('enter nickname', suggested);
        if (nickname == null)
            nickname = suggested;
        setCookie("username", nickname, 30);
        document.getElementById("nicknameField").value = nickname;
    } else {
        document.getElementById("nicknameField").value = savedNickname;
    }
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
      } else if (text.startsWith("/restart")) {
          logIt("Restarting Device");
          var mm = {"event":"restart", "username": document.getElementById('nicknameField').value};
          sendEvent(mm);
      } else {
          var mm = {"event":"chat", "text": text, "username": document.getElementById('nicknameField').value, "utc": Math.floor(Date.now() / 1000)};
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
