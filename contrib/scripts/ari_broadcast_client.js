#!/usr/bin/env node

/**
 * Example Node.js ARI client for StasisBroadcast
 *
 * This example demonstrates how to:
 * 1. Connect to Asterisk ARI WebSocket
 * 2. Listen for CallBroadcast events
 * 3. Claim channels based on routing logic
 * 4. Handle claimed channels
 *
 * Requirements:
 *   npm install ari-client
 *
 * Usage:
 *   node ari_broadcast_client.js <app_name> [asterisk_host] [username] [password]
 */

const client = require('ari-client');
const http = require('http');
const querystring = require('querystring');

// Simulate random network/processing delay when claiming (in milliseconds)
const MAX_CLAIM_DELAY_MS = 500;

// Configuration
const APP_NAME = process.argv[2] || 'sales_agent_1';
const ASTERISK_HOST = process.argv[3] || 'localhost:8088';
const USERNAME = process.argv[4] || 'asterisk';
const PASSWORD = process.argv[5] || 'asterisk';

// Parse host and port
const [host, port] = ASTERISK_HOST.split(':');

console.log('='.repeat(60));
console.log(`StasisBroadcast ARI Client`);
console.log('='.repeat(60));
console.log(`Application Name: ${APP_NAME}`);
console.log(`Asterisk Host:    ${host}:${port || 8088}`);
console.log('='.repeat(60));

/**
 * Claim a broadcast channel
 */
function claimChannel(channelId, appName) {
    return new Promise((resolve, reject) => {
        const queryParams = querystring.stringify({
            channelId: channelId,
            application: appName
        });

        const options = {
            hostname: host,
            port: port || 8088,
            path: `/ari/events/claim?${queryParams}`,
            method: 'POST',
            auth: `${USERNAME}:${PASSWORD}`
        };

        const req = http.request(options, (res) => {
            let data = '';

            res.on('data', (chunk) => {
                data += chunk;
            });

            res.on('end', () => {
                if (res.statusCode === 204) {
                    resolve({ success: true, channelId });
                } else if (res.statusCode === 409) {
                    resolve({ success: false, reason: 'already_claimed', channelId });
                } else if (res.statusCode === 404) {
                    resolve({ success: false, reason: 'not_found', channelId });
                } else {
                    reject(new Error(`Claim failed with status ${res.statusCode}: ${data}`));
                }
            });
        });

        req.on('error', (e) => {
            reject(e);
        });

        req.end();
    });
}

/**
 * Routing decision logic - customize this based on your needs
 */
function shouldClaimCall(event) {
    const caller = event.caller;
    const called = event.called;
    const variables = event.variables || {};

    console.log('  Evaluating routing logic...');
    console.log(`    Caller: ${caller || 'N/A'}`);
    console.log(`    Called: ${called || 'N/A'}`);
    console.log(`    Variables: ${JSON.stringify(variables, null, 2)}`);

    // Example routing logic - customize as needed:

    // 1. Sales agents only claim calls to sales numbers
    if (APP_NAME.startsWith('sales_')) {
        if (called && called.startsWith('1')) {
            console.log('  ✓ Matches sales criteria');
            return true;
        }
    }

    // 2. Support agents only claim support calls
    if (APP_NAME.startsWith('support_')) {
        if (called && called.startsWith('2')) {
            console.log('  ✓ Matches support criteria');
            return true;
        }
    }

    // 3. Check for specific channel variables
    if (variables.SKILL_REQUIRED === 'advanced' && APP_NAME.includes('advanced')) {
        console.log('  ✓ Matches skill requirement');
        return true;
    }

    // 4. Priority routing
    if (variables.PRIORITY === 'high' && APP_NAME.includes('premium')) {
        console.log('  ✓ Matches priority requirement');
        return true;
    }

    // Default: claim if no specific routing (for testing)
    if (APP_NAME.startsWith('test_')) {
        console.log('  ✓ Test mode - claiming all calls');
        return true;
    }

    console.log('  ✗ Does not match routing criteria');
    return false;
}

/**
 * Handle a claimed channel
 */
function handleClaimedChannel(ari, channel) {
    console.log(`\n[${channel.id}] Channel claimed successfully!`);
    console.log(`  Caller: ${channel.caller.number}`);
    console.log(`  State: ${channel.state}`);

    // Answer the channel
    ari.channels.answer({ channelId: channel.id }, (err) => {
        if (err) {
            console.error(`[${channel.id}] Error answering:`, err);
            return;
        }
        console.log(`[${channel.id}] Channel answered`);

        // Play a greeting
        ari.channels.play({
            channelId: channel.id,
            media: 'sound:hello-world'
        }, (err, playback) => {
            if (err) {
                console.error(`[${channel.id}] Error playing:`, err);
                return;
            }
            console.log(`[${channel.id}] Playing greeting`);

            // When playback finishes, hangup
            playback.on('PlaybackFinished', () => {
                console.log(`[${channel.id}] Playback finished, hanging up`);
                ari.channels.hangup({ channelId: channel.id }, () => {
                    console.log(`[${channel.id}] Hangup complete`);
                });
            });
        });
    });

    // Handle hangup
    channel.on('StasisEnd', () => {
        console.log(`[${channel.id}] Channel left application`);
    });

    channel.on('ChannelDestroyed', () => {
        console.log(`[${channel.id}] Channel destroyed`);
    });
}

/**
 * Main application
 */
client.connect(`http://${host}:${port || 8088}`, USERNAME, PASSWORD, (err, ari) => {
    if (err) {
        console.error('Error connecting to ARI:', err);
        process.exit(1);
    }

    console.log('\n✓ Connected to Asterisk ARI');
    console.log(`✓ Listening for broadcasts as "${APP_NAME}"\n`);

    // Start the application
    ari.start(APP_NAME, (err) => {
        if (err) {
            console.error('Error starting application:', err);
            process.exit(1);
        }

        console.log(`✓ Application "${APP_NAME}" started\n`);
        console.log('Waiting for broadcast events...\n');
    });

    // Listen for CallBroadcast events
    ari.on('CallBroadcast', async (event) => {
        const channel = event.channel;
        const channelId = channel.id;

        console.log('='.repeat(60));
        console.log(`📢 CallBroadcast received!`);
        console.log('='.repeat(60));
        console.log(`  Channel ID: ${channelId}`);
        console.log(`  Channel Name: ${channel.name}`);
        console.log(`  Caller: ${event.caller || 'N/A'}`);
        console.log(`  Called: ${event.called || 'N/A'}`);

        // Decide whether to claim this call
        if (shouldClaimCall(event)) {
            console.log(`\n  → Attempting to claim channel ${channelId}...`);

            try {
                await new Promise(resolve => setTimeout(resolve, Math.random() * MAX_CLAIM_DELAY_MS));
                const result = await claimChannel(channelId, APP_NAME);

                if (result.success) {
                    console.log(`  ✓ Claim successful!`);
                    // Note: The channel will arrive via StasisStart event
                } else {
                    console.log(`  ✗ Claim failed: ${result.reason}`);
                }
            } catch (error) {
                console.error(`  ✗ Error claiming channel:`, error.message);
            }
        } else {
            console.log(`\n  → Not claiming this call`);
        }

        console.log('='.repeat(60) + '\n');
    });

    // Listen for CallClaimed events (informational)
    ari.on('CallClaimed', (event) => {
        console.log(`📋 CallClaimed event: Channel ${event.channel.id} claimed by ${event.winner_app}`);
        if (event.winner_app === APP_NAME) {
            console.log(`   (That's us!)`);
        }
    });

    // Handle channels that enter our application
    ari.on('StasisStart', (event, channel) => {
        console.log(`\n📞 StasisStart: Channel ${channel.id} entered ${APP_NAME}`);
        handleClaimedChannel(ari, channel);
    });

    // Handle channels leaving
    ari.on('StasisEnd', (event, channel) => {
        console.log(`📵 StasisEnd: Channel ${channel.id} left ${APP_NAME}`);
    });

    // Handle errors
    ari.on('error', (err) => {
        console.error('ARI Error:', err);
    });

    // Handle WebSocket closure
    ari.on('WebSocketReconnecting', () => {
        console.log('⚠ WebSocket reconnecting...');
    });

    ari.on('WebSocketConnected', () => {
        console.log('✓ WebSocket connected');
    });
});

// Handle graceful shutdown
process.on('SIGINT', () => {
    console.log('\n\nShutting down gracefully...');
    process.exit(0);
});

console.log('\n(Press Ctrl+C to exit)\n');
