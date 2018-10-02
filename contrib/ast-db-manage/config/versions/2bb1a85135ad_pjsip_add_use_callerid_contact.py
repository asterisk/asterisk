"""pjsip add use_callerid_contact

Revision ID: 2bb1a85135ad
Revises: 7f85dd44c775
Create Date: 2018-10-18 15:13:40.462354

"""

# revision identifiers, used by Alembic.
revision = '2bb1a85135ad'
down_revision = '7f85dd44c775'

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

    op.add_column('ps_globals', sa.Column('use_callerid_contact', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_globals_use_callerid_contact_yesno_values','ps_globals')
    op.drop_column('ps_globals', 'use_callerid_contact')
