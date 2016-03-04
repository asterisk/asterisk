"""Add missing columns to system and registration

Revision ID: dbc44d5a908
Revises: 423f34ad36e2
Create Date: 2016-02-03 13:15:15.083043

"""

# revision identifiers, used by Alembic.
revision = 'dbc44d5a908'
down_revision = '423f34ad36e2'

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

    op.add_column('ps_systems', sa.Column('disable_tcp_switch', yesno_values))
    op.add_column('ps_registrations', sa.Column('line', yesno_values))
    op.add_column('ps_registrations', sa.Column('endpoint', sa.String(40)))

def downgrade():
    with op.batch_alter_table('ps_systems') as batch_op:
        batch_op.drop_column('disable_tcp_switch')
    with op.batch_alter_table('ps_registrations') as batch_op:
        batch_op.drop_column('line')
        batch_op.drop_column('endpoint')
