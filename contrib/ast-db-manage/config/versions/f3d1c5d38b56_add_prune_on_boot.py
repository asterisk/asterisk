"""add_prune_on_boot

Revision ID: f3d1c5d38b56
Revises: 44ccced114ce
Create Date: 2017-08-04 17:31:23.124767

"""

# revision identifiers, used by Alembic.
revision = 'f3d1c5d38b56'
down_revision = '44ccced114ce'

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

    op.add_column('ps_contacts', sa.Column('prune_on_boot', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_contacts_prune_on_boot_yesno_values', 'ps_contacts')
    op.drop_column('ps_contacts', 'prune_on_boot')
