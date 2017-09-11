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

"""make q member uniqueid autoinc

Revision ID: 5139253c0423
Revises: 1758e8bbf6b
Create Date: 2014-07-29 16:26:51.184981

"""

# revision identifiers, used by Alembic.
revision = '5139253c0423'
down_revision = '1758e8bbf6b'

from alembic import op
import sqlalchemy as sa


def upgrade():
    # Was unable to find a way to use op.alter_column() to add the unique
    # index property.
    op.drop_column('queue_members', 'uniqueid')
    op.add_column('queue_members', sa.Column(name='uniqueid', type_=sa.Integer,
                                             nullable=False, unique=True))
    # The postgres and mssql backends do not like the autoincrement needed for
    # mysql here.  It is just the backend that is giving a warning and
    # not the database itself.
    op.alter_column(table_name='queue_members', column_name='uniqueid',
                    existing_type=sa.Integer, existing_nullable=False,
                    autoincrement=True)


def downgrade():
    # Was unable to find a way to use op.alter_column() to remove the
    # unique index property.
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('uq_queue_members_uniqueid', 'queue_members')
    op.drop_column('queue_members', 'uniqueid')
    op.add_column('queue_members', sa.Column(name='uniqueid', type_=sa.String(80), nullable=False))
