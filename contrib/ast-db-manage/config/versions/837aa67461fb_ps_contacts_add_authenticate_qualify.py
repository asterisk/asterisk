"""ps_contacts add authenticate_qualify

Revision ID: 77a06d38881f
Revises: 65eb22eb195
Create Date: 2016-05-03 11:13:07.570192

"""

# revision identifiers, used by Alembic.
revision = '77a06d38881f'
down_revision = '65eb22eb195'

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

