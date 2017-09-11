"""add g726_non_standard

Revision ID: 28b8e71e541f
Revises: a541e0b5e89
Create Date: 2015-06-12 16:07:08.609628

"""

# revision identifiers, used by Alembic.
revision = '28b8e71e541f'
down_revision = 'a541e0b5e89'

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
    op.add_column('ps_endpoints', sa.Column('g726_non_standard', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_g726_non_standard_yesno_values','ps_endpoints')
    op.drop_column('ps_endpoints', 'g726_non_standard')
