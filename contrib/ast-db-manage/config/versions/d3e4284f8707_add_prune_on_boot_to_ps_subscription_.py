"""add prune_on_boot to ps_subscription_persistence

Revision ID: d3e4284f8707
Revises: 52798ad97bdf
Create Date: 2018-01-28 17:45:36.218123

"""

# revision identifiers, used by Alembic.
revision = 'd3e4284f8707'
down_revision = '52798ad97bdf'

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

    op.add_column('ps_subscription_persistence', sa.Column('prune_on_boot', yesno_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_subscription_persistence_prune_on_boot_yesno_values','ps_subscription_persistence')
    op.drop_column('ps_subscription_persistence', 'prune_on_boot')
