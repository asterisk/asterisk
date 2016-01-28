"""Add allow_reload to pjsip transport

Revision ID: 41bb5f0148d8
Revises: 423f34ad36e2
Create Date: 2016-01-28 16:47:29.616499

"""

# revision identifiers, used by Alembic.
revision = '41bb5f0148d8'
down_revision = '423f34ad36e2'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    op.add_column('ps_transports', sa.Column('allow_reload', yesno_values))
    pass

def downgrade():
    op.drop_column('ps_transports', 'allow_reload')
    pass
