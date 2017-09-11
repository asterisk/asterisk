"""add ps_endpoints.notify_early_inuse_ringing

Revision ID: d7983954dd96
Revises: 86bb1efa278d
Create Date: 2017-06-05 15:44:41.152280

"""

# revision identifiers, used by Alembic.
revision = 'd7983954dd96'
down_revision = '86bb1efa278d'

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

    op.add_column('ps_endpoints', sa.Column('notify_early_inuse_ringing', yesno_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_notify_early_inuse_ringing_yesno_values', 'ps_endpoints')
    op.drop_column('ps_endpoints', 'notify_early_inuse_ringing')
