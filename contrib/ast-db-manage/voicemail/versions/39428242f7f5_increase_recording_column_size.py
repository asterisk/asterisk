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

"""increase recording column size

Revision ID: 39428242f7f5
Revises: a2e9769475e
Create Date: 2014-07-28 16:02:05.104895

"""

# revision identifiers, used by Alembic.
revision = '39428242f7f5'
down_revision = 'a2e9769475e'

from alembic import op
import sqlalchemy as sa


def upgrade():
    # Make BLOB a LONGBLOB for mysql so recordings longer than about
    # four seconds can be stored.
    # See LargeBinary http://docs.sqlalchemy.org/en/rel_0_9/core/types.html
    op.alter_column('voicemail_messages', 'recording', type_=sa.LargeBinary(4294967295))


def downgrade():
    op.alter_column('voicemail_messages', 'recording', type_=sa.LargeBinary)
