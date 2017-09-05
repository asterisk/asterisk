"""add pjsip allow_overlap

Revision ID: 8fce4c573e15
Revises: f638dbe2eb23
Create Date: 2017-03-21 15:14:27.612945

"""

# revision identifiers, used by Alembic.
revision = '8fce4c573e15'
down_revision = 'f638dbe2eb23'

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

    op.add_column('ps_endpoints', sa.Column('allow_overlap', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_allow_overlap_yesno_values','ps_endpoints')
    op.drop_column('ps_endpoints', 'allow_overlap')
