"""empty message

Revision ID: 15db7b91a97a
Revises: 465e70e8c337
Create Date: 2017-03-08 16:56:38.108162

"""

# revision identifiers, used by Alembic.
revision = '15db7b91a97a'
down_revision = '465e70e8c337'

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

    op.add_column('ps_endpoints', sa.Column('rtcp_mux', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_rtcp_mux_yesno_values','ps_endpoints')
    op.drop_column('ps_endpoints', 'rtcp_mux')
