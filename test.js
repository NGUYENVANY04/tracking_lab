var mqtt = require('mqtt')
var client = mqtt.connect('mqtt://iot.bstar-badminton.com', {
    username: "bk0sfnqw1k12d49pc5hd"
})

var currentState = true; // Biến toàn cục để lưu trữ trạng thái hiện tại của thiết bị

client.on('connect', function () {
    console.log('connected');
    client.subscribe('v1/devices/me/rpc/request/+')
});

client.on('message', function (topic, message) {
    console.log('request.topic: ' + topic);
    console.log('request.body: ' + message.toString());
    var requestId = topic.slice('v1/devices/me/rpc/request/'.length);

    var parsedMessage = JSON.parse(message);

    if (parsedMessage["method"] === "getState") {
        // Trả về trạng thái hiện tại
        client.publish('v1/devices/me/rpc/response/' + requestId, JSON.stringify(currentState));
    }

    if (parsedMessage["method"] === "setState") {
        if (parsedMessage["params"] === false) {
            console.log('setState: OFF');
            currentState = false; // Cập nhật trạng thái
        } else {
            console.log('setState: ON');
            currentState = true; // Cập nhật trạng thái
        }
        // Gửi trạng thái cập nhật lên ThingsBoard
        client.publish('v1/devices/me/attributes', JSON.stringify({ "status": currentState }));
        client.publish('v1/devices/me/rpc/response/' + requestId, JSON.stringify(currentState));
    }
});

// Đoạn code này cho phép bạn thay đổi trạng thái từ terminal
process.stdin.resume();
process.stdin.setEncoding('utf8');
process.stdin.on('data', function (input) {
    input = input.trim();
    if (input === 'ON') {
        currentState = 1;
        console.log('State changed to ON');
    } else if (input === 'OFF') {
        currentState = 0;
        console.log('State changed to OFF');
    } else {
        console.log('Invalid command. Use "ON" or "OFF".');
    }
    // Gửi trạng thái cập nhật lên ThingsBoard
    client.publish('v1/devices/me/attributes', JSON.stringify({ "status": currentState }));
});
