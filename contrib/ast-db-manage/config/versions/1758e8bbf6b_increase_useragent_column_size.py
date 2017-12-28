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

"""increase useragent column size

Revision ID: 1758e8bbf6b
Revises: 1d50859ed02e
Create Date: 2014-07-28 14:04:17.874332

"""

# revision identifiers, used by Alembic.
revision = '1758e8bbf6b'
down_revision = '1d50859ed02e'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('sippeers', 'useragent', type_=sa.String(255))


def downgrade():
    op.alter_column('sippeers', 'useragent', type_=sa.String(20))
