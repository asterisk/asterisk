"""ps_globals add ignore_uri_user_options

Revision ID: a6ef36f1309
Revises: 4e2493ef32e6
Create Date: 2016-08-31 12:24:22.368956

"""

# revision identifiers, used by Alembic.
revision = 'a6ef36f1309'
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

    op.add_column('ps_globals', sa.Column('ignore_uri_user_options', yesno_values))


def downgrade():
    op.drop_column('ps_globals', 'ignore_uri_user_options')

