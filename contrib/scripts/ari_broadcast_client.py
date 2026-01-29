#!/usr/bin/env python3

"""
Example Python ARI client for StasisBroadcast

This example demonstrates how to:
1. Connect to Asterisk ARI WebSocket
2. Listen for CallBroadcast events
3. Claim channels based on routing logic
4. Handle claimed channels

Requirements:
    pip install websocket-client requests

Usage:
    python3 ari_broadcast_client.py <app_name> [asterisk_host] [username] [password]
"""

import sys
import json
import requests
import websocket
import logging
import time
import random

from typing import Dict, Any

# Simulate random network/processing delay when claiming (in seconds)
MAX_CLAIM_DELAY_SECONDS = 0.5

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(message)s'
)
logger = logging.getLogger(__name__)

# Configuration
APP_NAME = sys.argv[1] if len(sys.argv) > 1 else 'sales_agent_1'
ASTERISK_HOST = sys.argv[2] if len(sys.argv) > 2 else 'localhost:8088'
USERNAME = sys.argv[3] if len(sys.argv) > 3 else 'asterisk'
PASSWORD = sys.argv[4] if len(sys.argv) > 4 else 'asterisk'

# Parse host and port
if ':' in ASTERISK_HOST:
    host, port = ASTERISK_HOST.split(':')
else:
    host = ASTERISK_HOST
    port = '8088'

BASE_URL = f'http://{host}:{port}/ari'
WS_URL = f'ws://{host}:{port}/ari/events?app={APP_NAME}&api_key={USERNAME}:{PASSWORD}'

print('=' * 60)
print('StasisBroadcast ARI Client (Python)')
print('=' * 60)
print(f'Application Name: {APP_NAME}')
print(f'Asterisk Host:    {host}:{port}')
print('=' * 60)
print('\n(Press Ctrl+C to exit)\n')


def claim_channel(channel_id: str, app_name: str) -> Dict[str, Any]:
    """
    Claim a broadcast channel

    Args:
        channel_id: The ID of the channel to claim
        app_name: The name of the application claiming the channel

    Returns:
        Dictionary with 'success' and optional 'reason' keys
    """
    url = f'{BASE_URL}/events/claim'

    try:
        response = requests.post(
            url,
            params={
                'channelId': channel_id,
                'application': app_name
            },
            auth=(USERNAME, PASSWORD),
            timeout=5
        )

        if response.status_code == 204:
            return {'success': True, 'channel_id': channel_id}
        elif response.status_code == 409:
            return {'success': False, 'reason': 'already_claimed', 'channel_id': channel_id}
        elif response.status_code == 404:
            return {'success': False, 'reason': 'not_found', 'channel_id': channel_id}
        else:
            return {
                'success': False,
                'reason': f'status_{response.status_code}',
                'channel_id': channel_id
            }

    except Exception as e:
        logger.error(f'Error claiming channel: {e}')
        return {'success': False, 'reason': 'exception', 'error': str(e)}


def should_claim_call(event: Dict[str, Any]) -> bool:
    """
    Routing decision logic - customize this based on your needs

    Args:
        event: The CallBroadcast event

    Returns:
        True if this application should claim the call
    """
    caller = event.get('caller', 'N/A')
    called = event.get('called', 'N/A')
    variables = event.get('variables', {})

    logger.info('  Evaluating routing logic...')
    logger.info(f'    Caller: {caller}')
    logger.info(f'    Called: {called}')
    logger.info(f'    Variables: {json.dumps(variables, indent=2)}')

    # Example routing logic - customize as needed:

    # 1. Sales agents only claim calls to sales numbers
    if APP_NAME.startswith('sales_'):
        if called and called.startswith('1'):
            logger.info('  ✓ Matches sales criteria')
            return True

    # 2. Support agents only claim support calls
    if APP_NAME.startswith('support_'):
        if called and called.startswith('2'):
            logger.info('  ✓ Matches support criteria')
            return True

    # 3. Check for specific channel variables
    if variables.get('SKILL_REQUIRED') == 'advanced' and 'advanced' in APP_NAME:
        logger.info('  ✓ Matches skill requirement')
        return True

    # 4. Priority routing
    if variables.get('PRIORITY') == 'high' and 'premium' in APP_NAME:
        logger.info('  ✓ Matches priority requirement')
        return True

    # Default: claim if no specific routing (for testing)
    if APP_NAME.startswith('test_'):
        logger.info('  ✓ Test mode - claiming all calls')
        return True

    logger.info('  ✗ Does not match routing criteria')
    return False


def handle_claimed_channel(channel_dict: Dict[str, Any]):
    """
    Handle a claimed channel

    Args:
        channel_dict: Channel dictionary
    """
    channel_id = channel_dict['id']

    logger.info(f'\n[{channel_id}] Channel claimed successfully!')
    logger.info(f'  Caller: {channel_dict.get("caller", {}).get("number", "N/A")}')
    logger.info(f'  State: {channel_dict.get("state", "unknown")}')

    try:
        # Answer the channel
        requests.post(
            f'{BASE_URL}/channels/{channel_id}/answer',
            auth=(USERNAME, PASSWORD),
            timeout=5
        )
        logger.info(f'[{channel_id}] Channel answered')

        # Play a greeting
        requests.post(
            f'{BASE_URL}/channels/{channel_id}/play',
            params={'media': 'sound:hello-world'},
            auth=(USERNAME, PASSWORD),
            timeout=5
        )
        logger.info(f'[{channel_id}] Playing greeting')

    except Exception as e:
        logger.error(f'[{channel_id}] Error handling channel: {e}')


def on_call_broadcast(event: Dict[str, Any]):
    """
    Handle CallBroadcast events

    Args:
        event: The broadcast event
    """
    channel_id = event.get('channel', {}).get('id', 'unknown')
    channel_name = event.get('channel', {}).get('name', 'unknown')

    print('=' * 60)
    print('📢 CallBroadcast received!')
    print('=' * 60)
    print(f'  Channel ID: {channel_id}')
    print(f'  Channel Name: {channel_name}')
    print(f'  Caller: {event.get("caller", "N/A")}')
    print(f'  Called: {event.get("called", "N/A")}')

    # Decide whether to claim this call
    if should_claim_call(event):
        print(f'\n  → Attempting to claim channel {channel_id}...')

        time.sleep(random.uniform(0, MAX_CLAIM_DELAY_SECONDS))        
        result = claim_channel(channel_id, APP_NAME)

        if result['success']:
            print('  ✓ Claim successful!')
            # Note: The channel will arrive via StasisStart event
        else:
            print(f'  ✗ Claim failed: {result.get("reason", "unknown")}')
    else:
        print('\n  → Not claiming this call')

    print('=' * 60 + '\n')


def on_call_claimed(event: Dict[str, Any]):
    """
    Handle CallClaimed events (informational)

    Args:
        event: The claimed event
    """
    channel_id = event.get('channel', {}).get('id', 'unknown')
    winner_app = event.get('winner_app', 'unknown')

    logger.info(f'📋 CallClaimed event: Channel {channel_id} claimed by {winner_app}')
    if winner_app == APP_NAME:
        logger.info('   (That\'s us!)')


def on_stasis_start(event: Dict[str, Any]):
    """
    Handle StasisStart events

    Args:
        event: The start event
    """
    channel_dict = event.get('channel', {})
    channel_id = channel_dict.get('id', 'unknown')
    logger.info(f'\n📞 StasisStart: Channel {channel_id} entered {APP_NAME}')

    # Handle the channel
    handle_claimed_channel(channel_dict)


def on_stasis_end(event: Dict[str, Any]):
    """
    Handle StasisEnd events

    Args:
        event: The end event
    """
    channel_id = event.get('channel', {}).get('id', 'unknown')
    logger.info(f'📵 StasisEnd: Channel {channel_id} left {APP_NAME}')
    logger.info(f'[{channel_id}] Channel left application\n')


def on_playback_finished(event: Dict[str, Any]):
    """
    Handle PlaybackFinished events

    Args:
        event: The playback event
    """
    target_uri = event.get('playback', {}).get('target_uri', '')

    # Extract channel ID from target_uri (format: channel:CHANNEL_ID)
    if target_uri.startswith('channel:'):
        channel_id = target_uri.split(':', 1)[1]
        logger.info(f'[{channel_id}] Playback finished, hanging up')

        try:
            requests.delete(
                f'{BASE_URL}/channels/{channel_id}',
                auth=(USERNAME, PASSWORD),
                timeout=5
            )
            logger.info(f'[{channel_id}] Hangup complete')
        except Exception as e:
            logger.error(f'[{channel_id}] Error hanging up: {e}')


def on_message(ws, message):
    """Handle WebSocket messages"""
    try:
        event = json.loads(message)
        event_type = event.get('type')

        if event_type == 'CallBroadcast':
            on_call_broadcast(event)
        elif event_type == 'CallClaimed':
            on_call_claimed(event)
        elif event_type == 'StasisStart':
            on_stasis_start(event)
        elif event_type == 'StasisEnd':
            on_stasis_end(event)
        elif event_type == 'PlaybackFinished':
            on_playback_finished(event)

    except Exception as e:
        logger.error(f'Error handling message: {e}')


def on_error(ws, error):
    """Handle WebSocket errors"""
    logger.error(f'WebSocket error: {error}')


def on_close(ws, close_status_code, close_msg):
    """Handle WebSocket close"""
    logger.info('WebSocket connection closed')


def on_open(ws):
    """Handle WebSocket open"""
    logger.info('✓ WebSocket connected')
    logger.info(f'✓ Application "{APP_NAME}" started\n')
    logger.info('Waiting for broadcast events...\n')


def main():
    """Main application"""
    try:
        # Create WebSocket connection
        ws = websocket.WebSocketApp(
            WS_URL,
            on_open=on_open,
            on_message=on_message,
            on_error=on_error,
            on_close=on_close
        )

        logger.info('✓ Connected to Asterisk ARI')
        logger.info(f'✓ Listening for broadcasts as "{APP_NAME}"\n')

        # Run WebSocket in a blocking manner
        ws.run_forever()

    except KeyboardInterrupt:
        print('\n\nShutting down gracefully...')
        sys.exit(0)
    except Exception as e:
        logger.error(f'Error: {e}')
        sys.exit(1)


if __name__ == '__main__':
    main()
