"""add webrtc option to ps_endpoints

Revision ID: 44ccced114ce
Revises: 164abbd708c
Create Date: 2017-07-10 17:07:25.926150

"""

# revision identifiers, used by Alembic.
revision = '44ccced114ce'
down_revision = '164abbd708c'

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

    op.add_column('ps_endpoints', sa.Column('webrtc', yesno_values))


def downgrade():
    op.drop_column('ps_endpoints', 'webrtc')
