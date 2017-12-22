#
# Asterisk -- An open source telephony toolkit.
#
# Copyright (C) 2014, Richard Mudgett
#
# Richard Mudgett <rmudgett@digium.com>
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

"""Create CDR table.

Revision ID: 210693f3123d
Revises: None
Create Date: 2014-02-14 15:11:43.867292

"""

# revision identifiers, used by Alembic.
revision = '210693f3123d'
down_revision = None

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.create_table(
        'cdr',
        sa.Column('accountcode', sa.String(20)),
        sa.Column('src', sa.String(80)),
        sa.Column('dst', sa.String(80)),
        sa.Column('dcontext', sa.String(80)),
        sa.Column('clid', sa.String(80)),
        sa.Column('channel', sa.String(80)),
        sa.Column('dstchannel', sa.String(80)),
        sa.Column('lastapp', sa.String(80)),
        sa.Column('lastdata', sa.String(80)),
        sa.Column('start', sa.DateTime()),
        sa.Column('answer', sa.DateTime()),
        sa.Column('end', sa.DateTime()),
        sa.Column('duration', sa.Integer),
        sa.Column('billsec', sa.Integer),
        sa.Column('disposition', sa.String(45)),
        sa.Column('amaflags', sa.String(45)),
        sa.Column('userfield', sa.String(256)),
        sa.Column('uniqueid', sa.String(150)),
        sa.Column('linkedid', sa.String(150)),
        sa.Column('peeraccount', sa.String(20)),
        sa.Column('sequence', sa.Integer)
    )


def downgrade():
    op.drop_table('cdr')
