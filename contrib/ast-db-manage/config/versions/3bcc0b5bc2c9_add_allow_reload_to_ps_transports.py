"""Add allow_reload to ps_transports

Revision ID: 3bcc0b5bc2c9
Revises: dbc44d5a908
Create Date: 2016-02-05 17:43:39.183785

"""

# revision identifiers, used by Alembic.
revision = '3bcc0b5bc2c9'
down_revision = 'dbc44d5a908'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)
    op.add_column('ps_transports', sa.Column('allow_reload', yesno_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_transports_allow_reload_yesno_values','ps_transports')
    op.drop_column('ps_transports', 'allow_reload')
