"""ps_contacts add authenticate_qualify

Revision ID: 6be31516058d
Revises: 81b01a191a46
Create Date: 2016-05-03 14:57:12.538179

"""

# revision identifiers, used by Alembic.
revision = '6be31516058d'
down_revision = '81b01a191a46'

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

    op.add_column('ps_contacts', sa.Column('authenticate_qualify', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_contacts_authenticate_qualify_yesno_values','ps_contacts')
    op.drop_column('ps_contacts', 'authenticate_qualify')
