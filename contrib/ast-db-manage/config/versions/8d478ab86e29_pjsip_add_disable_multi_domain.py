"""pjsip_add_disable_multi_domain

Revision ID: 8d478ab86e29
Revises: 1c688d9a003c
Create Date: 2016-04-15 11:41:26.988997

"""

# revision identifiers, used by Alembic.
revision = '8d478ab86e29'
down_revision = '1c688d9a003c'

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

    op.add_column('ps_globals', sa.Column('disable_multi_domain', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_globals_disable_multi_domain_yesno_values','ps_globals')
    op.drop_column('ps_globals', 'disable_multi_domain')
