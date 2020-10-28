const fs        = require('fs');
const http      = require('http');
const WebSocket = require('ws');

let port = 9999;

const testServer = http.createServer(function(request, response) {
    response.end(fs.readFileSync(__dirname + '/index.html'));
});

const wss = new WebSocket.Server({server: testServer});

wss.on('connection', function connection(ws) {
    ws.on('message', function incoming(message) {
        console.log('received: %s', message);
    });

});

testServer.listen(port, '192.168.0.103', (err) => {
        if (err) {
            return console.log('something bad happened', err);
        }
        console.log(`server is listening on ${port}`);
    },
);