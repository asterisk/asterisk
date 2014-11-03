"""Add moh_passthrough option to pjsip

Revision ID: 339e1dfa644d
Revises: 1443687dda65
Create Date: 2014-10-21 14:55:34.197448

"""

# revision identifiers, used by Alembic.
revision = '339e1dfa644d'
down_revision = '1443687dda65'

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
    op.drop_column('ps_endpoints', 'moh_passthrough')
