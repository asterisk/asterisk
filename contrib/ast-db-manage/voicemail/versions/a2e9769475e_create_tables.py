#
# Asterisk -- An open source telephony toolkit.
#
# Copyright (C) 2013, Russell Bryant
#
# Russell Bryant <russell@russellbryant.net>
#
# See http://www.asterisk.org for more information about
# the Asterisk project. Please do not directly contact
# any of the maintainers of this project for assistance;
# the project provides a web site, mailing lists and IRC
# channels for your use.
#
# This program is free software, distributed under the terms of
# the GNU General Public License Version 2. See the LICENSE file
# at the top of the source tree.
#

"""Create tables

Revision ID: a2e9769475e
Revises: None
Create Date: 2013-07-29 23:43:09.431668

"""

# revision identifiers, used by Alembic.
revision = 'a2e9769475e'
down_revision = None

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.create_table(
        'voicemail_messages',
        sa.Column('dir', sa.String(255), nullable=False),
        sa.Column('msgnum', sa.Integer, nullable=False),
        sa.Column('context', sa.String(80)),
        sa.Column('macrocontext', sa.String(80)),
        sa.Column('callerid', sa.String(80)),
        sa.Column('origtime', sa.Integer),
        sa.Column('duration', sa.Integer),
        sa.Column('recording', sa.LargeBinary),
        sa.Column('flag', sa.String(30)),
        sa.Column('category', sa.String(30)),
        sa.Column('mailboxuser', sa.String(30)),
        sa.Column('mailboxcontext', sa.String(30)),
        sa.Column('msg_id', sa.String(40))
    )
    op.create_primary_key('voicemail_messages_dir_msgnum',
            'voicemail_messages', ['dir', 'msgnum'])
    op.create_index('voicemail_messages_dir', 'voicemail_messages', ['dir'])


def downgrade():
    op.drop_table('voicemail_messages')
