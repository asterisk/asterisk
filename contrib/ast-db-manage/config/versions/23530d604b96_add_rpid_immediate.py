#
# Asterisk -- An open source telephony toolkit.
#
# Copyright (C) 2015, Richard Mudgett
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

"""add rpid_immediate

Revision ID: 23530d604b96
Revises: 45e3f47c6c44
Create Date: 2015-03-18 17:41:58.055412

"""

# revision identifiers, used by Alembic.
revision = '23530d604b96'
down_revision = '45e3f47c6c44'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    ############################# Enums ##############################

    # yesno_values have already been created, so use postgres enum object
    # type to get around "already created" issue - works okay with mysql
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    op.add_column('ps_endpoints', sa.Column('rpid_immediate', yesno_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_rpid_immediate_yesno_values','ps_endpoints')
    op.drop_column('ps_endpoints', 'rpid_immediate')
