#
# Asterisk -- An open source telephony toolkit.
#
# Copyright (C) 2014, Jonathan Rose
#
# Jonathan R. Rose <jrose@digium.com>
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

"""Add Outgoing enum value to sippeers directmedia

Revision ID: 10aedae86a32
Revises: 5950038a6ead
Create Date: 2014-09-19 16:03:13.469436

"""

# revision identifiers, used by Alembic.
revision = '10aedae86a32'
down_revision = '5950038a6ead'

from alembic import op
from sqlalchemy.dialects.postgresql import ENUM
import sqlalchemy as sa

OLD_ENUM = ['yes', 'no', 'nonat', 'update']
NEW_ENUM = ['yes', 'no', 'nonat', 'update', 'outgoing']

old_type = sa.Enum(*OLD_ENUM, name='sip_directmedia_values')
new_type = sa.Enum(*NEW_ENUM, name='sip_directmedia_values_v2')

tcr = sa.sql.table('sippeers', sa.Column('directmedia', new_type,
                   nullable=True))

def upgrade():
    context = op.get_context()

    # Upgrading to this revision WILL clear your directmedia values.
    if context.bind.dialect.name != 'postgresql':
        op.alter_column('sippeers', 'directmedia',
                        type_=new_type,
                        existing_type=old_type)
    else:
        enum = ENUM("yes", "no", "nonat", "update", "outgoing",
                    name="sip_directmedia_values_v2")
        enum.create(op.get_bind(), checkfirst=False)

        op.execute('ALTER TABLE sippeers ALTER COLUMN directmedia TYPE'
                   ' sip_directmedia_values_v2 USING'
                   ' directmedia::text::sip_directmedia_values_v2')

        ENUM(name="sip_directmedia_values").drop(op.get_bind(), checkfirst=False)

def downgrade():
    context = op.get_context()

    op.execute(tcr.update().where(tcr.c.directmedia==u'outgoing')
               .values(directmedia=None))

    if context.bind.dialect.name != 'postgresql':
        op.alter_column('sippeers', 'directmedia',
                        type_=old_type,
                        existing_type=new_type)
    else:
        enum = ENUM("yes", "no", "nonat", "update",
                    name="sip_directmedia_values")
        enum.create(op.get_bind(), checkfirst=False)

        op.execute('ALTER TABLE sippeers ALTER COLUMN directmedia TYPE'
                   ' sip_directmedia_values USING'
                   ' directmedia::text::sip_directmedia_values')

        ENUM(name="sip_directmedia_values_v2").drop(op.get_bind(),
                                                checkfirst=False)
