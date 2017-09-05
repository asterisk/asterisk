"""pjsip: add global MWI options

Revision ID: c7a44a5a0851
Revises: 4a6c67fa9b7a
Create Date: 2016-08-03 15:08:22.524727

"""

# revision identifiers, used by Alembic.
revision = 'c7a44a5a0851'
down_revision = '4a6c67fa9b7a'

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

    op.add_column('ps_globals', sa.Column('mwi_tps_queue_high', sa.Integer))
    op.add_column('ps_globals', sa.Column('mwi_tps_queue_low', sa.Integer))
    op.add_column('ps_globals', sa.Column('mwi_disable_initial_unsolicited', yesno_values))

def downgrade():
    op.drop_column('ps_globals', 'mwi_tps_queue_high')
    op.drop_column('ps_globals', 'mwi_tps_queue_low')
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_globals_mwi_disable_initial_unsolicited_yesno_values','ps_globals')
    op.drop_column('ps_globals', 'mwi_disable_initial_unsolicited')
