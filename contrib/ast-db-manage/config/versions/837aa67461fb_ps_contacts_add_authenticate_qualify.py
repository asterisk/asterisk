"""ps_contacts add authenticate_qualify

Revision ID: 837aa67461fb
Revises: 8d478ab86e29
Create Date: 2016-04-27 16:26:59.381117

"""

# revision identifiers, used by Alembic.
revision = '837aa67461fb'
down_revision = '8d478ab86e29'

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

