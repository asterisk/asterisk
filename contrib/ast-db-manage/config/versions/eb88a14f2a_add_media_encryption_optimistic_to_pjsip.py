"""add media encryption optimistic to pjsip

Revision ID: eb88a14f2a
Revises: 10aedae86a32
Create Date: 2014-11-19 07:08:55.423018

"""

# revision identifiers, used by Alembic.
revision = 'eb88a14f2a'
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

    op.add_column('ps_endpoints', sa.Column('media_encryption_optimistic', yesno_values))


def downgrade():
    op.drop_column('ps_endpoints', 'media_encryption_optimistic')
