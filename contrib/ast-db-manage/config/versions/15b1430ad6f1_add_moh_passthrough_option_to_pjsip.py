"""add moh passthrough option to pjsip

Revision ID: 15b1430ad6f1
Revises: 371a3bf4143e
Create Date: 2014-11-19 07:44:51.225703

"""

# revision identifiers, used by Alembic.
revision = '15b1430ad6f1'
down_revision = '371a3bf4143e'

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

    op.add_column('ps_endpoints', sa.Column('moh_passthrough', yesno_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_moh_passthrough_yesno_values','ps_endpoints')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('moh_passthrough')
