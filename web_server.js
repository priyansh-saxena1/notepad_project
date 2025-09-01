const express = require('express');
const { spawn } = require('child_process');
const path = require('path');

const app = express();
const PORT = process.env.PORT || 3000;

// Middleware
app.use(express.json());
app.use(express.static('.')); // Serve static files from current directory

// Serve the main GUI
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'web_gui.html'));
});

// Test endpoint
app.get('/test', (req, res) => {
    res.status(200).send('Sender service is running');
});

// Send text endpoint
app.post('/send', async (req, res) => {
    try {
        const { text, receiverHost = 'localhost', receiverPort = 10000 } = req.body;
        
        if (!text) {
            return res.status(400).json({ error: 'Text is required' });
        }
        
        console.log(`Sending text to ${receiverHost}:${receiverPort}`);
        console.log(`Text: ${text.substring(0, 100)}${text.length > 100 ? '...' : ''}`);
        
        // Spawn the C++ sender process
        const senderProcess = spawn('./sender', [receiverHost, receiverPort.toString()], {
            stdio: ['pipe', 'pipe', 'pipe']
        });
        
        let stdout = '';
        let stderr = '';
        
        senderProcess.stdout.on('data', (data) => {
            stdout += data.toString();
        });
        
        senderProcess.stderr.on('data', (data) => {
            stderr += data.toString();
        });
        
        // Send the text to the sender process via stdin
        senderProcess.stdin.write(text);
        senderProcess.stdin.end();
        
        // Set a timeout for the process
        const timeout = setTimeout(() => {
            senderProcess.kill('SIGTERM');
        }, 30000); // 30 second timeout
        
        senderProcess.on('close', (code) => {
            clearTimeout(timeout);
            console.log(`Sender process exited with code ${code}`);
            console.log('STDOUT:', stdout);
            if (stderr) console.log('STDERR:', stderr);
            
            if (code === 0) {
                res.json({ 
                    success: true, 
                    message: 'Text sent successfully',
                    output: stdout
                });
            } else {
                res.status(500).json({ 
                    error: 'Failed to send text', 
                    code: code,
                    stderr: stderr 
                });
            }
        });
        
        senderProcess.on('error', (error) => {
            console.error('Failed to start sender process:', error);
            res.status(500).json({ 
                error: 'Failed to start sender process', 
                details: error.message 
            });
        });
        
    } catch (error) {
        console.error('Error in /send endpoint:', error);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.listen(PORT, () => {
    console.log(`Sender web service running on port ${PORT}`);
    console.log(`Access the notepad GUI at: http://localhost:${PORT}`);
});

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('Shutting down gracefully...');
    process.exit(0);
});

process.on('SIGTERM', () => {
    console.log('Shutting down gracefully...');
    process.exit(0);
});
