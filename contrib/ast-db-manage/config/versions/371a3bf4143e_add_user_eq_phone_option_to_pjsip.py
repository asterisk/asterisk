"""add user_eq_phone option to pjsip

Revision ID: 371a3bf4143e
Revises: 10aedae86a32
Create Date: 2014-10-13 13:46:24.474675

"""

# revision identifiers, used by Alembic.
revision = '371a3bf4143e'
down_revision = '10aedae86a32'

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

    op.add_column('ps_endpoints', sa.Column('user_eq_phone', yesno_values))

def downgrade():
    op.drop_column('ps_endpoints', 'user_eq_phone')
