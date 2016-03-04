"""add further dtls options

Revision ID: 51f8cb66540e
Revises: c6d929b23a8
Create Date: 2014-06-30 07:16:12.291684

"""

# revision identifiers, used by Alembic.
revision = '51f8cb66540e'
down_revision = 'c6d929b23a8'

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

    op.add_column('ps_endpoints', sa.Column('force_avp', yesno_values))
    op.add_column('ps_endpoints', sa.Column('media_use_received_transport', yesno_values))

def downgrade():
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('force_avp')
        batch_op.drop_column('media_use_received_transport')
