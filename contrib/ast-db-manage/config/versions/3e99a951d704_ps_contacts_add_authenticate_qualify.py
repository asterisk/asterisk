"""ps_contacts add authenticate_qualify

Revision ID: 3e99a951d704
Revises: 1c688d9a003c
Create Date: 2016-04-27 15:33:26.523156

"""

# revision identifiers, used by Alembic.
revision = '3e99a951d704'
down_revision = '1c688d9a003c'

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

    op.add_column('ps_contacts', sa.Column('authenticate_qualify', yesno_values))


def downgrade():
    op.drop_column('ps_contacts', 'authenticate_qualify')

