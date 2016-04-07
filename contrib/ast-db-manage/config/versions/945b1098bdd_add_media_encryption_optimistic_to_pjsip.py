"""add media encryption optimistic to pjsip

Revision ID: 945b1098bdd
Revises: 15b1430ad6f1
Create Date: 2014-11-19 07:47:52.490388

"""

# revision identifiers, used by Alembic.
revision = '945b1098bdd'
down_revision = '15b1430ad6f1'

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
