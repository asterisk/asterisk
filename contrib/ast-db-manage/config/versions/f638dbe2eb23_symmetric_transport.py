"""symmetric_transport

Revision ID: f638dbe2eb23
Revises: 15db7b91a97a
Create Date: 2017-03-09 09:38:59.513479

"""

# revision identifiers, used by Alembic.
revision = 'f638dbe2eb23'
down_revision = '15db7b91a97a'

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

    op.add_column('ps_transports', sa.Column('symmetric_transport', yesno_values))
    op.add_column('ps_subscription_persistence', sa.Column('contact_uri', sa.String(256)))

def downgrade():
    op.drop_column('ps_subscription_persistence', 'contact_uri')
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_transports_symmetric_transport_yesno_values','ps_transports')
    op.drop_column('ps_transports', 'symmetric_transport')
