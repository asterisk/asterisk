"""add preferred_codec_only option to pjsip

Revision ID: 7f3e21abe318
Revises: 4e2493ef32e6
Create Date: 2016-09-02 11:00:23.534748

"""

# revision identifiers, used by Alembic.
revision = '7f3e21abe318'
down_revision = '4e2493ef32e6'

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

    op.add_column('ps_endpoints', sa.Column('preferred_codec_only', yesno_values))

def downgrade():
    op.drop_column('ps_endpoints', 'preferred_codec_only')
