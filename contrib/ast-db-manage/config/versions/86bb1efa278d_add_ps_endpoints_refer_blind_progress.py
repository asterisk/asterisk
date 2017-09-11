"""add ps_endpoints.refer_blind_progress

Revision ID: 86bb1efa278d
Revises: 1d0e332c32af
Create Date: 2017-05-08 16:52:36.615161

"""

# revision identifiers, used by Alembic.
revision = '86bb1efa278d'
down_revision = '1d0e332c32af'

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

    op.add_column('ps_endpoints', sa.Column('refer_blind_progress', yesno_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_refer_blind_progress_yesno_values','ps_endpoints')
    op.drop_column('ps_endpoints', 'refer_blind_progress')
