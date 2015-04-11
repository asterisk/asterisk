#
# Asterisk -- An open source telephony toolkit.
#
# Copyright (C) 2015, Fairview 5 Engineering, LLC
#
# George Joseph <george.joseph@fairview5.com>
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

"""add_pjsip_qualify_timeout

Revision ID: 2256a84ca226
Revises: 23530d604b96
Create Date: 2015-04-03 13:18:18.023787

"""

# revision identifiers, used by Alembic.
revision = '2256a84ca226'
down_revision = '23530d604b96'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_aors', sa.Column('qualify_timeout', sa.Integer))
    op.add_column('ps_contacts', sa.Column('qualify_timeout', sa.Integer))
    pass


def downgrade():
    op.drop_column('ps_aors', 'qualify_timeout')
    op.drop_column('ps_contacts', 'qualify_timeout')
    pass
