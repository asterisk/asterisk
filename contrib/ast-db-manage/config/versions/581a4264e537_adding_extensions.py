#
# Asterisk -- An open source telephony toolkit.
#
# Copyright (C) 2013, Digium, Inc.
#
# Scott Griepentrog <sgriepentrog@digium.com>
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

"""adding extensions

Revision ID: 581a4264e537
Revises: 43956d550a44
Create Date: 2013-12-10 16:32:41.145327

"""

# revision identifiers, used by Alembic.
revision = '581a4264e537'
down_revision = '43956d550a44'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.create_table(
        'extensions',
        sa.Column('id', sa.BigInteger, primary_key=True, nullable=False,
                  unique=True, autoincrement=True),
        sa.Column('context', sa.String(40), primary_key=True, nullable=False),
        sa.Column('exten', sa.String(40), primary_key=True, nullable=False),
        sa.Column('priority', sa.Integer, primary_key=True, nullable=False,
                  autoincrement=True),
        sa.Column('app', sa.String(40), nullable=False),
        sa.Column('appdata', sa.String(256), nullable=False),
    )


def downgrade():
    op.drop_table('extensions')
